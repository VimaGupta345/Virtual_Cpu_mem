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

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE


void CPUScheduler(virConnectPtr conn,int interval);

//Custom structures to store the cpu time and mapping information of the vcpus
typedef struct Vcpu_stats{
	unsigned long long int cpuTime;
	int cpu_pin;
	int index; 
}Vcpu_stats;

typedef struct Vcpu_util{
	unsigned long long int v_util;
	int vcpu_to_pcpu_pin;
}Vcpu_util;


Vcpu_stats* curr_stats;
Vcpu_stats* prev_stats;

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
	// Get the total number of pCpus in the host
	signal(SIGINT, signal_callback_handler);
	
	virDomainPtr *domains;
	int num_vcpus = virConnectListAllDomains(conn,&domains,VIR_CONNECT_LIST_DOMAINS_ACTIVE);
	curr_stats = calloc(num_vcpus,sizeof(Vcpu_stats));
        prev_stats = calloc(num_vcpus,sizeof(Vcpu_stats));

	while(!is_exit)
	// Run the CpuScheduler function that checks the CPU Usage and sets the pin at an interval of "interval" seconds
	{
		//printf("Scheduler is called");
		CPUScheduler(conn, interval);
		sleep(interval);
	}

	// Closing the connection
	virConnectClose(conn);
	return 0;
}
int compare(const void *a, const void *b) {
	return (((Vcpu_util*)b)->v_util - ((Vcpu_util*)a)->v_util);
}



/* COMPLETE THE IMPLEMENTATION */
void CPUScheduler(virConnectPtr conn, int interval)
{
	//Find the pointer to active domains based on given connection pointer	
	virDomainPtr *domains;
	int num_vcpus = virConnectListAllDomains(conn,&domains,VIR_CONNECT_LIST_DOMAINS_ACTIVE);

	for(int i = 0; i < num_vcpus; i++){
		prev_stats[i].cpuTime = curr_stats[i].cpuTime;
		prev_stats[i].cpu_pin = curr_stats[i].cpu_pin;
	}
	
	virVcpuInfoPtr info = (virVcpuInfoPtr)calloc(num_vcpus,sizeof(virVcpuInfo));
	int maxinfo = num_vcpus;
	unsigned char * cpumaps = NULL;
	int maplen = 0;//Assuming one byte per map
	for(int i = 0; i < num_vcpus; i++){
		virDomainGetVcpus(*(domains+i),info,maxinfo,cpumaps,0);
		curr_stats[i].cpuTime = info[0].cpuTime;
	        curr_stats[i].cpu_pin = info[0].cpu;
		curr_stats[i].index = i;	
	}

	//Now that we have curr and prev stats from VM's, we can now update curr_stats with utilization numbers
	Vcpu_util* v_list = calloc(num_vcpus,sizeof(Vcpu_util));
	for(int i = 0; i < num_vcpus; i++){
		v_list[i].v_util = ((curr_stats[i].cpuTime - prev_stats[i].cpuTime)*100)/(interval*pow(10,9));
		v_list[i].vcpu_to_pcpu_pin = curr_stats[i].cpu_pin;
	}

	//Physical cpu info:
	unsigned int num_pcpus;
	int n_pcpu = virNodeGetCPUMap(conn,NULL,&num_pcpus,0);
	n_pcpu = (int)n_pcpu;	
	//Now that we have virtual utilization, we can now find the utilization per physical cpu
	unsigned long long int* p_util;
	int* p_pin;
	p_util = (unsigned long long int*)calloc(num_pcpus,sizeof(unsigned long long int));
	p_pin = (int*)calloc(num_pcpus,sizeof(int));

	for(int i = 0; i < num_vcpus; i++){
		p_util[curr_stats[i].cpu_pin] += v_list[i].v_util;
		//Keeping a track of number of Vcpu pinned to a particular p cpu
		p_pin[curr_stats[i].cpu_pin] += 1;
	}
	
	//Sum of subsets is denoted by p_util array
	unsigned long long int average_util = 0;
	for(int i = 0; i < num_pcpus; i++){
		average_util += p_util[i];   
	}
	average_util = average_util/num_pcpus;

	int balance = 0;
	for(int i = 0; i < num_pcpus; i++){
		if(abs(p_util[i] - average_util) > 5){
			balance = 1;
			break;
		}
	}
	

	if(balance){
	qsort (v_list, num_vcpus, sizeof(Vcpu_util), compare);
	for(int k = 0; k < num_pcpus; k++){
		p_util[k] = 0;
	}
	for(int i = 0; i < num_vcpus; i++){
		long long unsigned int min_val = ULLONG_MAX;
		int min_index = 0;
	       	for(int j = 0; j < num_pcpus; j++){
			if(p_util[j] < min_val){
				min_index = j;
				min_val = p_util[j];
			}
		}
		maplen = (num_pcpus%8) ? (num_pcpus/8) + 1 : (num_pcpus/8);
		unsigned char* cpumap_pin = calloc(maplen,sizeof(unsigned char));
		*cpumap_pin = (unsigned char)(1 << min_index); 
		if(v_list[i].vcpu_to_pcpu_pin != min_index){
			virDomainPinVcpu(*(domains+i),0,cpumap_pin,maplen);
		}
		p_util[min_index] += v_list[i].v_util;
		v_list[i].vcpu_to_pcpu_pin = min_index;
		free(cpumap_pin);
		}
	}

}
