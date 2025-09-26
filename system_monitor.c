#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
    #include <pdh.h>
#elif defined(__APPLE__)
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/sysctl.h>
    #include <mach/mach.h>
    #include <mach/processor_info.h>
    #include <mach/mach_host.h>
    #include <mach/vm_statistics.h>
    #include <mach/machine.h>
#else  // Linux
    #include <unistd.h>
    #include <sys/sysinfo.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <string.h>
#endif

// Forward declarations
void get_memory_info(unsigned long long *total_mem, unsigned long long *used_mem, 
                    unsigned long long *free_mem);
void format_memory_size(unsigned long long bytes, char *buffer);

// Function to get system load averages
void get_load_average(double loadavg[3]) {
#ifdef _WIN32
    // Windows doesn't have a direct equivalent to load average
    // Using CPU usage as a rough approximation
    loadavg[0] = loadavg[1] = loadavg[2] = 0.0;
#else
    if (getloadavg(loadavg, 3) < 0) {
        printf("Error getting load average\n");
        loadavg[0] = loadavg[1] = loadavg[2] = 0.0;
    }
#endif
}

// Function to get CPU usage
double get_cpu_usage() {
#ifdef _WIN32
    static PDH_HQUERY cpuQuery;
    static PDH_HCOUNTER cpuTotal;
    static int init = 0;
    
    if (!init) {
        PdhOpenQuery(NULL, 0, &cpuQuery);
        PdhAddCounter(cpuQuery, L"\\Processor(_Total)\\% Processor Time", 0, &cpuTotal);
        PdhCollectQueryData(cpuQuery);
        init = 1;
        return 0.0;
    }
    
    PDH_FMT_COUNTERVALUE counterVal;
    
    PdhCollectQueryData(cpuQuery);
    PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal);
    return counterVal.doubleValue;

#elif defined(__APPLE__)
    natural_t num_processors;
    processor_info_array_t processor_info;
    mach_msg_type_number_t num_info;
    
    host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &num_processors,
                       &processor_info, &num_info);
    
    unsigned long long total_user = 0, total_system = 0, total_idle = 0;
    
    for (natural_t i = 0; i < num_processors; i++) {
        unsigned long offset = CPU_STATE_MAX * i;
        total_user += processor_info[offset + CPU_STATE_USER] + processor_info[offset + CPU_STATE_NICE];
        total_system += processor_info[offset + CPU_STATE_SYSTEM];
        total_idle += processor_info[offset + CPU_STATE_IDLE];
    }
    
    vm_deallocate(mach_task_self(), (vm_address_t)processor_info, num_info * sizeof(int));
    
    static unsigned long long prev_user = 0, prev_system = 0, prev_idle = 0;
    
    if (prev_user == 0) {
        prev_user = total_user;
        prev_system = total_system;
        prev_idle = total_idle;
        return 0.0;
    }
    
    unsigned long long total_delta = (total_user - prev_user) + 
                                   (total_system - prev_system) + 
                                   (total_idle - prev_idle);
                                   
    double cpu_use = 100.0 * (1.0 - (double)(total_idle - prev_idle) / total_delta);
    
    prev_user = total_user;
    prev_system = total_system;
    prev_idle = total_idle;
    
    return cpu_use;

#else // Linux
    static unsigned long long prev_total = 0, prev_idle = 0;
    unsigned long long total = 0, idle = 0;
    FILE* file = fopen("/proc/stat", "r");
    
    if (file == NULL) {
        return 0.0;
    }
    
    char line[256];
    if (fgets(line, sizeof(line), file)) {
        char cpu[10];
        unsigned long long user, nice, system, idle_time, iowait, irq, softirq, steal;
        sscanf(line, "%s %llu %llu %llu %llu %llu %llu %llu %llu",
               cpu, &user, &nice, &system, &idle_time, &iowait, &irq, &softirq, &steal);
        
        idle = idle_time + iowait;
        total = user + nice + system + idle_time + iowait + irq + softirq + steal;
    }
    
    fclose(file);
    
    if (prev_total == 0) {
        prev_total = total;
        prev_idle = idle;
        return 0.0;
    }
    
    unsigned long long total_delta = total - prev_total;
    unsigned long long idle_delta = idle - prev_idle;
    
    prev_total = total;
    prev_idle = idle;
    
    return 100.0 * (1.0 - ((double)idle_delta / total_delta));
#endif
}

// Function to format memory size in human-readable format
// Function to get memory information
void get_memory_info(unsigned long long *total_mem, unsigned long long *used_mem, 
                    unsigned long long *free_mem) {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    
    *total_mem = memInfo.ullTotalPhys;
    *free_mem = memInfo.ullAvailPhys;
    *used_mem = *total_mem - *free_mem;

#elif defined(__APPLE__)
    vm_size_t page_size;
    mach_port_t mach_port;
    mach_msg_type_number_t count;
    vm_statistics64_data_t vm_stats;
    
    mach_port = mach_host_self();
    count = sizeof(vm_stats) / sizeof(natural_t);
    
    if (HOST_VM_INFO64 == host_statistics64(mach_port, HOST_VM_INFO64,
                                          (host_info64_t)&vm_stats, &count)) {
        printf("Error getting VM statistics\n");
        return;
    }
    
    host_page_size(mach_port, &page_size);
    
    unsigned long long free_count = vm_stats.free_count;
    unsigned long long active_count = vm_stats.active_count;
    unsigned long long inactive_count = vm_stats.inactive_count;
    unsigned long long wire_count = vm_stats.wire_count;
    
    *total_mem = (free_count + active_count + inactive_count + wire_count) * page_size;
    *free_mem = free_count * page_size;
    *used_mem = *total_mem - *free_mem;

#else // Linux
    struct sysinfo si;
    
    if (sysinfo(&si) != 0) {
        printf("Error getting system info\n");
        return;
    }
    
    *total_mem = si.totalram * si.mem_unit;
    *free_mem = si.freeram * si.mem_unit;
    *used_mem = *total_mem - *free_mem;
#endif
}

// Function to format memory size
void format_memory_size(unsigned long long bytes, char* buffer) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int i = 0;
    double size = bytes;
    
    while (size >= 1024 && i < 3) {
        size /= 1024;
        i++;
    }
    
    sprintf(buffer, "%.2f %s", size, units[i]);
}

int main() {
    double loadavg[3];
    unsigned long long total_mem, used_mem, free_mem;
    char mem_buffer[50];
    
    printf("\033[2J\033[H"); // Clear screen
#ifdef _WIN32
    printf("Windows System Monitor\n");
#elif defined(__APPLE__)
    printf("macOS System Monitor\n");
#else
    printf("Linux System Monitor\n");
#endif
    printf("==================\n\n");
    
    while(1) {
        // Get and display CPU usage
        double cpu_usage = get_cpu_usage();
        printf("\033[4;0H"); // Move cursor to line 4
        printf("CPU Usage: %.2f%%   \n", cpu_usage);
        
        // Get and display load averages
        get_load_average(loadavg);
        printf("Load Averages: %.2f (1 min), %.2f (5 min), %.2f (15 min)   \n",
               loadavg[0], loadavg[1], loadavg[2]);
        
        // Get and display memory information
        get_memory_info(&total_mem, &used_mem, &free_mem);
        
        format_memory_size(total_mem, mem_buffer);
        printf("Total Memory: %s   \n", mem_buffer);
        
        format_memory_size(used_mem, mem_buffer);
        printf("Used Memory: %s   \n", mem_buffer);
        
        format_memory_size(free_mem, mem_buffer);
        printf("Free Memory: %s   \n", mem_buffer);
        
        fflush(stdout);
        sleep(1); // Update every second
    }
    
    return 0;
}