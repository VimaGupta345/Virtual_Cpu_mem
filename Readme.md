## Project Instruction Updates:

1. Complete the function CPUScheduler() in vcpu_scheduler.c
2. If you are adding extra files, make sure to modify Makefile accordingly.
3. Compile the code using the command `make all`
4. You can run the code by `./vcpu_scheduler <interval>`
5. While submitting, write your algorithm and logic in this Readme.

Step 1: Query the stats at beginning (previous) of the time interval and at the end of the interval (cuurent) and normalize with time interval to find the VCPU utilization  for each virtual CPU.

Step 2: Find the corresponding utilization for each physical CPU by adding utilizations from all virtual CPU mapped to that PCPU based on VCPU->PCPU pinning.

Step 3: The condition for balancing checks how much the physical CPU utilization deviates from the average and goes on to next steps if the deviation is over 5%.

Step 4: We iterate over each virtual CPU. For each virtual CPU, we find the physical CPU with least utilization and pin the current VCPU to that PCPU. The balancing appraoch is based on greedy number partitioning algorithm where we start off by assuming that no piinings exist initially. The VCPU is mapped to PCPU with minimal utilization.

Step 5: We prevent unnecessary repinning by ensuring that if a VCPU is mapped to a given PCPU, we don't attempt to repin those. The balancing check further prevents any unnecessary balancing.
