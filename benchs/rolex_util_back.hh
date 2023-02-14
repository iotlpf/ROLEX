#pragma once

#include <time.h>
#include <sched.h>
#include <unistd.h>

#include "r2/src/logging.hh"

#define NS_PER_S 1000000000.0
#define TIMER_DECLARE(n) struct timespec b##n,e##n
#define TIMER_BEGIN(n) clock_gettime(CLOCK_MONOTONIC, &b##n)
#define TIMER_END_NS(n,t) clock_gettime(CLOCK_MONOTONIC, &e##n); \
    (t)=(e##n.tv_sec-b##n.tv_sec)*NS_PER_S+(e##n.tv_nsec-b##n.tv_nsec)
#define TIMER_END_S(n,t) clock_gettime(CLOCK_MONOTONIC, &e##n); \
    (t)=(e##n.tv_sec-b##n.tv_sec)+(e##n.tv_nsec-b##n.tv_nsec)/NS_PER_S


namespace rolex {

using namespace r2;

// 8-byte value
using KeyType = u64;
using ValType = u64;

/**
 * @brief The id of RPCs in RPCCOre
 * 
 */
enum RPCId {
  GET = 0, PUT, UPDATE, DELETE, SCAN
};

struct __attribute__((packed)) ReplyValue {
  bool status;         /// The queried data exists? or other operation success?
  ValType val;         /// The returned value
};

#define CACHELINE_SIZE (1 << 6)
struct alignas(CACHELINE_SIZE) ThreadParam {
    uint64_t throughput;
    uint32_t thread_id;
};
using thread_param_t = ThreadParam;




struct MonitorParam {
  pthread_t proc_n;      // the thread number
  int interval;          // the time of interval
};
using monitor_param_t = MonitorParam;

/**
 * @brief monitor the cpu utilization 
 * 
 * @param argv monitor_param_t
 */
void* cpu_monitor(void *argv) {
  monitor_param_t param = *(monitor_param_t*)argv;
  LOG(3) << "Hello cpu_monitor";
  char cmd[1024];
  sprintf(cmd, "ps -p %d -o %%cpu,%%mem | awk NR==2>>log", (unsigned int)param.proc_n);
  //system("echo > log");
  while(1) {
    //system(cmd);
    sleep(param.interval);
  }
  /*
  unsigned int proc_n = *(unsigned int*)argv;
  FILE *fp = NULL;
  char cmd[1024];
  char buf[1024];
  char result[4096];
  sprintf(cmd, "echo > cpu_log; watch -n1 -t 'ps -p %d -o %%cpu,%%mem | awk NR==2>>cpu_log' ", proc_n);
  if( (fp = popen(cmd, "r")) != NULL)
  {
      while(fgets(buf, 1024, fp) != NULL)
      {
          strcat(result, buf);
      }
      pclose(fp);
      fp = NULL;
  }*/
}

} // namespace rolex