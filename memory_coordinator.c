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

int is_exit = 0; // DO NOT MODIFY THE VARIABLE

void MemoryScheduler(virConnectPtr conn,int interval);

typedef struct memory_util{
	unsigned long long int avl_mem;
	unsigned long long int unused_mem;
	unsigned long long int balloon_mem;
	unsigned long long int usable_mem;
	unsigned long long int max_mem;
	int crash;
}memory_util;


typedef struct host_mem{
	unsigned long long int total;
	unsigned long long int free;
}host_mem;

//typedef struct setInfo{
//	unsigned long int maxMem;
//}setInfo;

memory_util* curr_mem_util;
memory_util* prev_mem_util;
/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	printf("Caught Signal");
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
	
	virDomainPtr *domains;
        int num_vcpus = virConnectListAllDomains(conn,&domains,VIR_CONNECT_LIST_DOMAINS_ACTIVE);
        curr_mem_util = calloc(num_vcpus,sizeof(memory_util));
        prev_mem_util = calloc(num_vcpus,sizeof(memory_util));

	while(!is_exit)
	{
		// Calls the MemoryScheduler function after every 'interval' seconds
		MemoryScheduler(conn, interval);
		sleep(interval);
	}
	free(curr_mem_util);
	free(prev_mem_util);

	// Close the connection
	virConnectClose(conn);
	return 0;
}

/*
COMPLETE THE IMPLEMENTATION
*/
void MemoryScheduler(virConnectPtr conn, int interval)
{
	virDomainPtr *domains;
        int num_vcpus = virConnectListAllDomains(conn,&domains,VIR_CONNECT_LIST_DOMAINS_ACTIVE);
	
	int mem_per = virDomainSetMemoryStatsPeriod(*domains,interval, VIR_DOMAIN_AFFECT_CURRENT);
	mem_per = (int)mem_per;
 	virDomainMemoryStatPtr stats;
	stats = (virDomainMemoryStatPtr)calloc(VIR_DOMAIN_MEMORY_STAT_NR,sizeof(virDomainMemoryStatStruct));
	

	for(int i = 0; i < num_vcpus; i++){
        virDomainMemoryStats(*(domains+i),stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);
		for(int j = 0; j < VIR_DOMAIN_MEMORY_STAT_NR; j++){
			if(stats[j].tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE){
				curr_mem_util[i].avl_mem = stats[j].val;
			}
			if(stats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED){
				curr_mem_util[i].unused_mem = stats[j].val;
			}
			if(stats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON){
				curr_mem_util[i].balloon_mem = stats[j].val;
			}
			if(stats[j].tag == VIR_DOMAIN_MEMORY_STAT_USABLE){
                                curr_mem_util[i].usable_mem = stats[j].val;
			}	
		}
		curr_mem_util[i].max_mem = virDomainGetMaxMemory(*(domains+i));
	}

	unsigned int num_pcpus;
        int n_pcpu = virNodeGetCPUMap(conn,NULL,&num_pcpus,0);
	n_pcpu = (int)n_pcpu;

	//The host machine stats are
        virNodeMemoryStatsPtr params;
	params = calloc(num_pcpus, sizeof(virNodeMemoryStats));	
	int nparams = 0;
	int cellNum = VIR_NODE_MEMORY_STATS_ALL_CELLS;
	if (virNodeGetMemoryStats(conn, cellNum, NULL, &nparams, 0) == 0 && nparams != 0){
		params = calloc(nparams,sizeof(virNodeMemoryStats));
    		virNodeGetMemoryStats(conn,cellNum, params, &nparams, 0);
	}
	host_mem* host = calloc(1,sizeof(host_mem));
	for(int i = 0; i < nparams; i++){
		if(strcmp(params[i].field,VIR_NODE_MEMORY_STATS_TOTAL) == 0)
			host->total = params[i].value;
		if(strcmp(params[i].field,VIR_NODE_MEMORY_STATS_FREE) == 0)
			host->free = params[i].value;
	}
	virDomainInfoPtr info;
	info = calloc(num_vcpus,sizeof(virDomainInfo));
	for(int i = 0; i < num_vcpus; i++){
		virDomainGetInfo(*(domains+i),info);
	
	}


	//Assigning memory thresholds using KB in entire code	
	unsigned long long int Host_mem_threshold = 200*1024;
	unsigned long long int VM_mem_threshold = 100*1024;

	//Determine condition as to when memory assigning is needed!	
	int max_index = 0;
	for(int i = 0; i < num_vcpus; i++){
		//Step 1: Identify if VM is in need, unused mem is the identifier
		if(curr_mem_util[i].unused_mem < VM_mem_threshold){//modified threshold?
			//Step 2: Identify max contender based on unused being greater than threshold
			unsigned long long int max_val = 0;
		        max_index = 0;	
			for(int j = 0; j < num_vcpus; j++){
				if(i == j)
					continue;
				long long unsigned int mem_to_give = curr_mem_util[j].unused_mem - VM_mem_threshold;
				if(curr_mem_util[j].unused_mem > VM_mem_threshold && mem_to_give > max_val){
						max_val = mem_to_give;
						max_index = j;
					}
			}
			float per = 0.5;
			if((per*max_val + curr_mem_util[i].balloon_mem <= info->maxMem) && (VM_mem_threshold <  curr_mem_util[max_index].unused_mem - per*max_val)
				       	&& (curr_mem_util[i].unused_mem + per*max_val > VM_mem_threshold)){
				virDomainSetMemory(*(domains+max_index),curr_mem_util[max_index].balloon_mem - per*max_val); 
				virDomainSetMemory(*(domains+i),curr_mem_util[i].balloon_mem + per*max_val);
			}
			else if(host->free - VM_mem_threshold > Host_mem_threshold){
				long long unsigned int host_mem_min = MIN(VM_mem_threshold,(info->maxMem - curr_mem_util[i].balloon_mem));
				
				virDomainSetMemory(*(domains+i),curr_mem_util[i].balloon_mem + host_mem_min);
			}
			else{
			}
		}
		else{
                        if(curr_mem_util[i].unused_mem > 3*VM_mem_threshold){//when to release memory
                               		virDomainSetMemory(*(domains+i),curr_mem_util[i].balloon_mem - (0.5/2)*VM_mem_threshold);
                        	}
                	}

			int k = i;
			virDomainMemoryStats(*(domains+i),stats,VIR_DOMAIN_MEMORY_STAT_NR, 0);
			for(int l = 0; l < VIR_DOMAIN_MEMORY_STAT_NR; l++){
                                 			if(stats[l].tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE){
                                                                 curr_mem_util[k].avl_mem = stats[l].val;
                                                         }
                                                         if(stats[l].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED){
                                                                 curr_mem_util[k].unused_mem = stats[l].val;
                                                         }
                                                         if(stats[l].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON){
                                                                 curr_mem_util[k].balloon_mem = stats[l].val;
                                                         }
                                                         if(stats[l].tag == VIR_DOMAIN_MEMORY_STAT_USABLE){
                                                                 curr_mem_util[k].usable_mem = stats[l].val;
                                                         }
                                                 }
			k = max_index;
			virDomainMemoryStats(*(domains+max_index),stats,VIR_DOMAIN_MEMORY_STAT_NR, 0);
                        for(int l = 0; l < VIR_DOMAIN_MEMORY_STAT_NR; l++){
                                                        if(stats[l].tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE){
                                                                 curr_mem_util[k].avl_mem = stats[l].val;
                                                         }
                                                         if(stats[l].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED){
                                                                 curr_mem_util[k].unused_mem = stats[l].val;
                                                         }
                                                         if(stats[l].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON){
                                                                 curr_mem_util[k].balloon_mem = stats[l].val;
                                                         }
                                                         if(stats[l].tag == VIR_DOMAIN_MEMORY_STAT_USABLE){
                                                                 curr_mem_util[k].usable_mem = stats[l].val;
                                                         }
                                                 }
	
			//Update host stats
			virNodeGetMemoryStats(conn,cellNum, params, &nparams, 0);
			for(int p = 0; p < nparams; p++){
                		 if(strcmp(params[p].field,VIR_NODE_MEMORY_STATS_TOTAL) == 0)
		                         host->total = params[p].value;
	                         if(strcmp(params[p].field,VIR_NODE_MEMORY_STATS_FREE) == 0)
                        	 	host->free = params[p].value;
         		}

	}

	free(stats);
	free(host);
	free(info);
	free(params);

}

			
			






