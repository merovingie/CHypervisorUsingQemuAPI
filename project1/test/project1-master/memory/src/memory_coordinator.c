#include<stdio.h>
#include<stdlib.h>
#include<libvirt/libvirt.h>
#include<math.h>
#include<string.h>
#include<unistd.h>
#include<limits.h>
#include<signal.h>
#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)
#define PRINT_VAR_STR(X) printf(#X" value is %s at Address %p\n", X, &X)
#define PRINT_VAR_INT(X) printf(#X" value is %llu at Address %p\n", X, &X)


int is_exit = 0; // DO NOT MODIFY THE VARIABLE

static const double starvThres =  0.2;
static const double wasteThres = 0.35;  
static const double PENALTY = 0.3; 
static const double hostGenThres = 8.0;
static const unsigned long minHostMem = 400 * 1024;
static const unsigned long minDomMem = 350 * 1024;

virDomainPtr *domains;

struct DomLinkedNode {
	virDomainPtr dom;
	struct DomLinkedNode *next;
	unsigned long activeMem;
	unsigned long availableMem;
};


//get domainsNUmber
int getDomainsNumber(virConnectPtr conn){
	virDomainPtr *domains;
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING;
	int domsNum;
	domsNum = virConnectListAllDomains(conn,&domains,flags);
	return domsNum;
}

//get domain name
const char* getName(virConnectPtr conn, int domNum){
	virDomainPtr *domains;
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING;
	int domsNum;
	domsNum = virConnectListAllDomains(conn,&domains,flags);
	virDomainInfo domainInfo;
        char* name = virDomainGetName(domains[domNum]);
	return name;
}

//setup linked nodes of current and next structures of domain stats to set
void setUpNode(struct DomLinkedNode **head, struct DomLinkedNode **cur, virDomainPtr dom,
		unsigned long activeMem, unsigned long availableMem) {
	if (!(*head)) {
		*head = (struct DomLinkedNode *)calloc(1, sizeof(struct DomLinkedNode));
		*cur = *head;
	} else {
		struct DomLinkedNode *newNode = (struct DomLinkedNode *)calloc(1, sizeof(struct DomLinkedNode));
		(*cur)->next = newNode;
		(*cur) = (*cur)->next;
	}
	(*cur)->next = NULL;
	(*cur)->dom = dom;
	(*cur)->activeMem = activeMem;
	(*cur)->availableMem = availableMem;
	return;
}


void MemoryScheduler(virConnectPtr conn,int interval);

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	printf("Caught Signal\n");
	is_exit = 1;
}

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
int main(int argc, char *argv[])
{
	virConnectPtr conn;

	if(argc != 2)
	{
		printf("Incorrect number of arguments\n");
		return 0;
	}

	// Gets the interval passes as a command line argument and sets it as the STATS_PERIOD for collection of balloon memory statistics of the domains
	int interval = atoi(argv[1]);
	
	conn = virConnectOpen("qemu:///system");
	if(conn == NULL)
	{
		fprintf(stderr, "Failed to open connection\n");
		return 1;
	}

	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	{
		// Calls the MemoryScheduler function after every 'interval' seconds
		MemoryScheduler(conn, interval);
		sleep(interval);
	}

	// Close the connection
	virConnectClose(conn);
	return 0;
}

/*
COMPLETE THE IMPLEMENTATION
*/
void MemoryScheduler(virConnectPtr conn, int interval)
{
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING;
	int domsNum = virConnectListAllDomains(conn, &domains, flags);
	if(domsNum < 0 ){
		printf("Faiiled to Get the List of Domains\n");
	}else{
		printf("I have domain info of %d domains\n", domsNum);
	}
	struct DomLinkedNode *starvHead = NULL, *starvCur = NULL, *wasteHead = NULL, *wasteCur = NULL, *cur = NULL;
	int n_nodes[2] = {0};
	for (int i = 0; i < domsNum; i++) {
		virDomainMemoryStatStruct mem_stats[VIR_DOMAIN_MEMORY_STAT_NR];
		unsigned int flags = VIR_DOMAIN_AFFECT_CURRENT;
		//Dynamically change the domain memory balloon driver statistics collection period to 1
		//Returns 0 in case of success
		if (virDomainSetMemoryStatsPeriod(domains[i], 1, flags)
				== -1) {
			printf("failed to change balloon collection period for domain %s\n",
					virDomainGetName(domains[i]));
		}
		// collects domain stats the same way the cpu stats gets collected 
		// by returning a dynamic set of stats supported by setting nr_stats(u call first time to get the nparams for cpu) 
		// vs nr_params = VIR_DOMAIN_MEMORY_STAT_NR
		if (virDomainMemoryStats(domains[i], mem_stats,
					VIR_DOMAIN_MEMORY_STAT_NR, 0) == -1) {
			printf("failed to collect memory stats of domain %s\n",
					virDomainGetName(domains[i]));
		}
		unsigned long long actual;
		unsigned long long available;
		unsigned long long unused;
		for (int j = 0; j < VIR_DOMAIN_MEMORY_STAT_NR; j++) {
			if (mem_stats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) {
				actual = mem_stats[j].val;
			} else if (mem_stats[j].tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE) {
				available = mem_stats[j].val;
			} else if (mem_stats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED) {
				unused = mem_stats[j].val;
			}

		}
		printf("Domain %s:\n actual balloon size is:-> %lu   available memory:-> %lu    unused memory:-> %lu\n", virDomainGetName(domains[i]),(unsigned long)(actual / 1024),(unsigned long)(available / 1024),(unsigned long)(unused / 1024));


		//setting up nodes of starve and wasteful domains	
		if (unused < starvThres * actual) {
			setUpNode(&starvHead, &starvCur, domains[i],
					actual,
					unused);
			n_nodes[0]++;
		} else if (unused > wasteThres * actual) {
			setUpNode(&wasteHead, &wasteCur, domains[i],
					actual,
					unused);
			n_nodes[1]++;
		}
	}
	printf("Number of starving domains:-> %d\nNumber of wasteful domains:-> %d\n", n_nodes[0],n_nodes[1]);
	if (starvHead) {
		unsigned long totalNeed = 0;
		cur = starvHead;
		for (int i = 0; i < n_nodes[0]; i++) {
			totalNeed += (starvThres * cur->activeMem) - cur->availableMem;
			cur = cur->next;
		}
		printf("Starving domains need %lu MB in total.\n", totalNeed / 1024);
		unsigned long totalRelease = 0;
		if (wasteHead) {
			cur = wasteHead;
			for (int i = 0; i < n_nodes[1]; i++) {
				unsigned long alloc = cur->activeMem - cur->availableMem * PENALTY;
				if (alloc < minDomMem) {
					alloc = minDomMem;
				}
				virDomainSetMemory(cur->dom, alloc);
				totalRelease += (cur->activeMem - alloc);
				printf("Wasteful domain %s to Deflate memory from %lu to %lu\n",
						virDomainGetName(cur->dom), cur->activeMem / 1024, alloc / 1024);
				cur = cur->next;
			}
		}
		printf("Total memory release by wasteful vms is %lu.\n", totalRelease / 1024);
		if (totalRelease < totalNeed) {
			unsigned long nodeProv = hostGenThres * (totalNeed - totalRelease);
			unsigned long nodeFree = virNodeGetFreeMemory(conn) / 1024 - minHostMem;
			if (nodeFree <= 0) {
				printf("failed to provide memory from host this cycle.\n");
			} else {
				if (nodeProv > nodeFree) {
					nodeProv = nodeFree;
				}
				totalRelease += nodeProv;
				printf("Total release after adding host is %lu.\n", totalRelease / 1024);
			}
		}
		unsigned long provPerNode = totalRelease / n_nodes[0];
		cur = starvHead;
		for (int i = 0; i < n_nodes[0]; i++) {
			unsigned long alloc = cur->activeMem + provPerNode;
			unsigned long maxMemory = virDomainGetMaxMemory(cur->dom);
			if (alloc > maxMemory) {
				alloc = maxMemory;
			}
			virDomainSetMemory(cur->dom, alloc);
			printf("domain %s starving --> Inflating Memory from %lu to %lu \n", virDomainGetName(cur->dom), cur->activeMem / 1024, alloc / 1024);
			cur = cur->next;
		}
	}
	else if (wasteHead) {
			printf("No starving domain. Putting back memory to host.\n");
		cur = wasteHead;
		for (int i = 0; i < n_nodes[1]; i++) {
			unsigned long alloc = cur->activeMem - cur->availableMem * PENALTY;
			if (alloc < minDomMem) {
				alloc = minDomMem;
			}
			virDomainSetMemory(cur->dom, alloc);
			printf("domain %s wasteful --> Deflating memory %lu to %lu to host.\n",
					virDomainGetName(cur->dom), cur->activeMem / 1024, alloc / 1024);
			cur = cur->next;
		}
	}
	printf("Finished coordination!");
	sleep(interval);

									            


}
