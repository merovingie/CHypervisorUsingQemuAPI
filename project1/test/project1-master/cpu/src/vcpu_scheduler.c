#include<stdio.h>
#include<stdlib.h>
#include<libvirt/libvirt.h>
#include<math.h>
#include<string.h>
#include<unistd.h>
#include<limits.h>
#include<signal.h>
#include <float.h>
#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)
#define PRINT_VAR_STR(X) printf(#X" value is %s at Address %p\n", X, &X)
#define PRINT_VAR_INT(X) printf(#X" value is %llu at Address %p\n", X, &X)

static const long converToSec = 1000000000;

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE

int prevDomainsNum = 0;

int totNumVCpus = 0;

double threshold = 20.0;


typedef struct domainStats {
	    virDomainPtr dom;
	    int vcpusNum;
	    unsigned long long *vcpusTime;
	    int *pCpus;
}domstats ;

typedef struct Vcpu {
	    virDomainPtr dom;
	    int vcpuNum;
}vcpu;


struct domainStats *currentDomainStats, *prevDomainStats;

//push cpu bit and mapping --> function deleted no need for it
//calculate virtual cputime average usage between previous and current in percentage
double usage(unsigned long long int diff, double period) {
	    return diff / period * 100;
}

/*CPU PINNING
I started by pinning the least busy vcpu to the busiest pcpu and in the last iteration of the code i created a whole array of vcpu 
and pcpu in order of busiest to freest for physical and freest to busiest in virtual and mapped to each other.
my plan was to build a specifuc function that divide virtual to physical or physical to virtual depending on the number of virtual to physical
but no time and we assumed that its one virtual to physical.. so it's good like that.. there is still remanant of code from the first iterations 
and development and i dont have enough clean things up so sorry about that but i think it's working*/
void pinVcpu(double *physicalCpuUsage, int numpCpus, struct domainStats *currentDomainStats,struct domainStats *prevDomainStats, int domainsNum, double period){
	int busiestCpu;
	int arrange_busiest = 0;
	int freestCpu;
	double busiestUse = 0.0;
	double freestUse = DBL_MAX;
	double arrange_busy = 0.0 ;
	int domainCpu[domainsNum];
	
	//create max and lower ceiling .. use the macro
	for (int i = 0; i < numpCpus; i++) {
		printf("this is physicalCpuUsage %f\n",physicalCpuUsage[i]);
		if (physicalCpuUsage[i] > busiestUse) {
			busiestUse = physicalCpuUsage[i];
			busiestCpu = i;
		}
		if (physicalCpuUsage[i] < freestUse) {
			freestUse = physicalCpuUsage[i];
			freestCpu = i;
		}
	}

	int loopcount = 0;
	while(1){
		for(int i = 0; i < numpCpus; i++){
			//printf("physical cpu[i] in small loop: %f\n", physicalCpuUsage[i]);
			if(physicalCpuUsage[i] > arrange_busy){
				arrange_busy = physicalCpuUsage[i];
				//printf("arrange_busy: %f\n", arrange_busy);
				arrange_busiest = i;
				//printf("arrange_busiest: %d\n", arrange_busiest);
			}
		}
		domainCpu[loopcount] = arrange_busiest;
		//printf("domainCpu[loopcount]: %d\n", domainCpu[loopcount]);
		if(loopcount > (numpCpus-1)) break;
		//printf("physical cpu[i]: %f\n", physicalCpuUsage[arrange_busiest]);
		physicalCpuUsage[arrange_busiest] = -1.0;
		//printf("physical cpu[i]: %f\n", physicalCpuUsage[arrange_busiest]);
		loopcount++;
		arrange_busy = -1.0;

	}
	for(int i = 0; i < domainsNum; i++){
		printf("this element %d in array value %d\n", i, domainCpu[i]);
	}
	size_t cpuMapLength = VIR_CPU_MAPLEN(numpCpus);
	unsigned char dCpumap = 0x1;
	if ((busiestUse - freestUse) < threshold) {
		printf("Balanced enough for threshold between busiest and freest = %f.No need to change\n", threshold);
		for (int i = 0; i < domainsNum; i++) {
			for (int j = 0; j < currentDomainStats[i].vcpusNum; j++) {
				dCpumap = 0x1 << currentDomainStats[i].pCpus[j];
				virDomainPinVcpu(currentDomainStats[i].dom, j, &dCpumap, cpuMapLength);
			}
		}
		return;
	}
	printf("Freest cpu %d has usage %f,\n Busiest cpu %d has usage %f.\n", freestCpu, freestUse, busiestCpu, busiestUse);
	struct Vcpu leastVcpu = {0};
	double leastuse = DBL_MAX;
	double use = 0.0;
	int totNumVcpus = 0;
	double arrange_free = DBL_MAX;
        int arrange_freest;
	for (int i = 0; i < domainsNum; i++) {
		for (int j = 0; j < currentDomainStats[i].vcpusNum; j++) {
			totNumVcpus +=1;
			if (currentDomainStats[i].pCpus[j] == busiestCpu) {
				use =  usage(currentDomainStats[i].vcpusTime[j] - prevDomainStats[i].vcpusTime[j], period);
				if (use < leastuse) {
					leastuse = use;
					leastVcpu.dom = currentDomainStats[i].dom;
					leastVcpu.vcpuNum = j;
				}
			}
		}
	}

	printf("total number of virtual CPUz %d\n", totNumVcpus);
	
	double *virtualCpuUsage = (double *)calloc(totNumVcpus, sizeof(double));
	int virtualCpu[totNumVcpus];
		
	for (int i = 0; i < domainsNum; i++) {
		for (int j = 0; j < currentDomainStats[i].vcpusNum; j++) {
			//printf("currentDomainStats[i].vcpusNum: %d\n",  currentDomainStats[i].vcpusNum); 
			use =  usage(currentDomainStats[i].vcpusTime[j] - prevDomainStats[i].vcpusTime[j], period);
			virtualCpuUsage[i+j] += use;	
			}
		}

	for(int i = 0; i < totNumVcpus; i++){
		printf("this element %d in array of virtual cpu usage value %f\n", i, virtualCpuUsage[i]);
	}
	


	loopcount = 0;
	while(1){
		for(int i = 0; i < totNumVcpus; i++){
			if(virtualCpuUsage[i] < arrange_free){
				arrange_free = virtualCpuUsage[i];
				arrange_freest = i;
			}
		}
		virtualCpu[loopcount] = arrange_freest;
		if(loopcount > (totNumVcpus-1)) break;
		virtualCpuUsage[arrange_freest] = DBL_MAX;
		loopcount++;
		arrange_free = DBL_MAX;
	}
	for(int i = 0; i < totNumVcpus; i++){
		printf("this element %d in array of virtualCpu numbers value %d\n", i, virtualCpu[i]);
	}

	for(int i=0; i < (totNumVcpus); i++){
		//unsigned char freestCpumap = 0x1 << virtualCpu[i];
		unsigned char busiestCpumap = 0x1 << domainCpu[i];	
		printf("Pinning least busy vcpu in order %d in domain %s to most busy physical cpu in order %d\n", virtualCpu[i],
		virDomainGetName(currentDomainStats[virtualCpu[i]].dom), domainCpu[i]);
		if (virDomainPinVcpu(currentDomainStats[virtualCpu[i]].dom, ((currentDomainStats[i].vcpusNum) -1), &busiestCpumap, cpuMapLength) == 0) {
			printf("Pinning finished\n");
		} else {
			printf("Pinning failed\n");
		};
		}
	free(virtualCpuUsage);

	return;
}


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

//get total Vcpus in all domains
int getTotVCpus(virConnectPtr conn){
	virDomainPtr *domains;
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING;
	int totVcpus = 0;
	int domsNum;
	//create domains objects and get their pointer structure
	domsNum = virConnectListAllDomains(conn,&domains,flags);
	for(int i = 0; i < domsNum; i++){
	if(domsNum < 0 ){
		printf("Failed to Get the List of Domains\n");
	}else{
		printf("I have domain info of %d domains\n", domsNum);
	}
	totVcpus += virDomainGetVcpusFlags(domains[i], VIR_DOMAIN_AFFECT_LIVE);
	}
	return totVcpus;
}

//set Structure Domstats
int setdomainStats(virConnectPtr conn, int numpCpus, struct domainStats *dom_stats) {
	virDomainPtr *domains;
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING;
	int domsNum;
	//create domains objects and get their pointer structure
	domsNum = virConnectListAllDomains(conn,&domains,flags);
	if(domsNum < 0 ){
		printf("Failed to Get the List of Domains\n");
	}else{
		printf("I have domain info of %d domains\n", domsNum);
	}
	unsigned char *cpumaps;
	size_t cpuMapLength;
	for (int i = 0; i < domsNum; i++) {
		virVcpuInfoPtr info_vcpu;
		virDomainInfo domainInfo;
		int virInfoNum;
		// get domain info using domain objects.. no racing conditions according to the api man
		virInfoNum = virDomainGetInfo(domains[i], &domainInfo);
		int vcpusNum = 0;
		//get virtual cpu info
		if ((vcpusNum = virDomainGetVcpusFlags(domains[i], VIR_DOMAIN_AFFECT_LIVE)) == -1) {
			printf("Function virDomainGetVcpusFlags returns failed.. possible Domains have changed during processing\n");
		}
		//allocate data and create vcpu map then collect data
		dom_stats[i].vcpusTime = (unsigned long long *)calloc(vcpusNum, sizeof(unsigned long long));
		dom_stats[i].pCpus = (int *)calloc(vcpusNum, sizeof(int));
		info_vcpu = (virVcpuInfoPtr)calloc(vcpusNum, sizeof(virVcpuInfo));
		cpuMapLength = VIR_CPU_MAPLEN(numpCpus);
		cpumaps = (unsigned char *)calloc(vcpusNum, cpuMapLength);
		if (virDomainGetVcpus(domains[i], info_vcpu, vcpusNum, cpumaps, cpuMapLength) == -1) {
			printf("Could not retrieve Vir Cpus info\n");
		}
		for (int j = 0; j < vcpusNum; j++) {
			dom_stats[i].vcpusTime[j] = info_vcpu[j].cpuTime;
		    	printf("this is CPUTIME: %llu\n",dom_stats[i].vcpusTime[j]);
		    	dom_stats[i].pCpus[j] = info_vcpu[j].cpu;
		    	printf("this is the physical Cpu: %llu\n",dom_stats[i].pCpus[j]);
		    }
		dom_stats[i].dom = domains[i];
		dom_stats[i].vcpusNum = vcpusNum;
		//virDomainFree(domains[i]);
	    }
	//free(domains);
	return domsNum;
}

// get Total CPU time 
unsigned long long pCpuTime(virConnectPtr conn) { 
	int nr_params = 0;
       	int nr_cpus = VIR_NODE_CPU_STATS_ALL_CPUS; 
	virNodeCPUStatsPtr params; 
	unsigned long long busy_time = 0; 
	if(virNodeGetCPUStats(conn, nr_cpus, NULL, &nr_params, 0) == 0 && nr_params != 0) printf("Got return of Num of paramters"); 
	params = malloc(sizeof(virNodeCPUStats) * nr_params); 
	memset(params, 0, sizeof(virNodeCPUStats) * nr_params); 
	if(virNodeGetCPUStats(conn, nr_cpus, params, &nr_params, 0) == 0) printf("Success on Getting CPU Params"); 
	for (int i = 0; i < nr_params; i++) { 
		if (strcmp(params[i].field, VIR_NODE_CPU_STATS_USER) == 0 || strcmp(params[i].field, VIR_NODE_CPU_STATS_KERNEL) == 0) { 
			busy_time += params[i].value; 
		} 
		printf("pCPUs %s: %llu ns\n", params[i].field, params[i].value);
       	} 
	free(params); 
	printf("pCPUs busy time: %llu ns\n", busy_time);
       	return busy_time; 
}

//Get virtual cpu count
unsigned int getPcpus(virConnectPtr conn) {
	    virNodeInfo info;
	    virNodeGetInfo(conn, &info);
	    return info.cpus;
}


void CPUScheduler(virConnectPtr conn,int interval);

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

	// Get the total number of pCpus in the host
	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	// Run the CpuScheduler function that checks the CPU Usage and sets the pin at an interval of "interval" seconds
	{
		CPUScheduler(conn, interval);
		sleep(interval);
	}

	// Closing the connection
	virConnectClose(conn);
	return 0;
}

/* COMPLETE THE IMPLEMENTATION */
void CPUScheduler(virConnectPtr conn, int interval)
{
	//char *c = virConnectGetCapabilities(conn);
	//char *z = virConnectGetHostname(conn);
	//int f = virConnectGetMaxVcpus(conn, "kvm");
	//testing out put by using macro PRINT_ deleted no need for it.. not gonna use them 
	
	//collect Host Node info and CPU number
	int numpCpus = getPcpus(conn);
	printf("Number of Physical CPUS  : %u\n",numpCpus);

	//instialize array for physical CPU
	double *physicalCpuUsage = (double *)calloc(numpCpus, sizeof(double));//lord in heaven free it omg
	totNumVCpus = getTotVCpus(conn);
	if(!physicalCpuUsage){
		printf("here we go segementation fault!\n");
		return(1);
	}


	
	//collect cpu affinity structures
	//I know i should have collected the domain and domain numbers data as global variables or structure
	//I did this way and it's working .. so sorry.. 
	//I was wondering if there is an implementation of kvm on arm like a rasperry pi but if not why dont we write that in python!
	//I mean if it is then c is fast and i can run that on a cluster of rasperry pis! maybe i should ask on the forums!
	pCpuTime(conn);
	// get number of domains
	int domsNum;
	domsNum = getDomainsNumber(conn);

	//interval in nanosecond
	double period = interval * converToSec;
	
	//intialize domain stats structure
	currentDomainStats = (struct domainStats *)calloc(domsNum, sizeof(struct domainStats));

	//collect domain structure data
	int domsNum_after = setdomainStats(conn, numpCpus, currentDomainStats);
	if (domsNum_after != prevDomainsNum || domsNum_after != domsNum) {
		printf("Number of Dimains has changed...\n");
		prevDomainStats = (struct domainStats *)calloc(domsNum_after, sizeof(struct domainStats));
	}else {
	printf("Number of Domains stays the same...Calculating\n");
	double use = 0.0;
	PRINT_VAR_INT(domsNum_after);
	for (int i = 0; i < domsNum_after; i++) {
		for (int j = 0; j < currentDomainStats[i].vcpusNum; j++) {
			use =  usage(currentDomainStats[i].vcpusTime[j] - prevDomainStats[i].vcpusTime[j], period);
			printf("Domain %s 's %d vcpu has usage %f during this period, \n",getName(conn, i), j, use);
			physicalCpuUsage[currentDomainStats[i].pCpus[j]] += use;
			printf("currentDomainStats[i].pCpus[j]: %d\n", currentDomainStats[i].pCpus[j]);
			printf("physicalCpuUsage[currentDomainStats[i].pCpus[j]]: %f\n", physicalCpuUsage[currentDomainStats[i].pCpus[j]]);
		}
	}
	//calculate and pin virtual cpus to physical cpus
	pinVcpu(physicalCpuUsage, numpCpus, currentDomainStats, prevDomainStats, domsNum_after, period);
	}
	memcpy(prevDomainStats, currentDomainStats, domsNum_after * sizeof(struct domainStats));
	// store most recent domains number in the first instance of domain number to compare in time in three instances..
	// there is a better .. try if you have time to change it to just one comparison no need for ocd
	prevDomainsNum = domsNum_after;
	printf("*****Finished scheduling and sleeping for interval*****\n");
	free(physicalCpuUsage);
	sleep(interval);
	printf("--------------------------------------------------\n");
}	
	//virDomainFree(domains[i]);
	//free(domains);
