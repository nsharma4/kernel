#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// MY TESTING FILE !!
// This program will run each of the three test programs (forktest, ls, and usertests) with both fixed and dynamic tick intervals, and report the average performance metrics for each.

// Function to run a test program and measure its performance
void run_test(char *prog_name, char *args[], int mode, int repeat) {
  struct perf_metrics start_metrics, end_metrics;
  int i, pid;
  int total_ticks = 0, total_ctx_switches = 0;
  
  printf("Testing %s in %s mode (average of %d runs):\n", 
         prog_name, mode ? "dynamic tick" : "fixed tick", repeat);
  
  // Set tick mode
  set_tick_mode(mode);
  
  for (i = 0; i < repeat; i++) {
    printf("Starting run %d...\n", i+1);
    
    // Get initial metrics
    get_perf_metrics(&start_metrics);
    
    // Run the test program
    pid = fork();
    if (pid < 0) {
      printf("fork failed\n");
      exit(1);
    }
    
    if (pid == 0) {
      // Child process - execute the test program
      exec(prog_name, args);
      printf("exec failed\n");
      exit(1);
    } else {
      // Parent processwait for child to complete
      printf("Waiting for child process %d...\n", pid);
      wait(0);
      printf("Child process %d completed\n", pid);
      
      // Get final metrics
      get_perf_metrics(&end_metrics);
      
      // Accumulate results
      total_ticks += (end_metrics.total_ticks - start_metrics.total_ticks);
      total_ctx_switches += (end_metrics.context_switches - start_metrics.context_switches);
      
      printf("Run %d complete\n", i+1);
    }
  }
  
  // Print average results
  printf("  Average ticks: %d\n", total_ticks / repeat);
  printf("  Average context switches: %d\n", total_ctx_switches / repeat);
  printf("  Tick interval: %lu (%s)\n", 
         mode ? end_metrics.current_tick_interval : 1000000UL,
         mode ? "dynamic" : "fixed");
}

int
main(int argc, char *argv[])
{
  int repeat = 10; // Default to 10 runs for each test
  
  if (argc > 1) {
    repeat = atoi(argv[1]);
    if (repeat <= 0) repeat = 10;
  }
  
  printf("=== Runtime Performance Test ===\n");
  printf("Testing dynamic tick interval vs. fixed tick interval\n");
  printf("Running each test %d times and reporting averages\n\n", repeat);
  
  // Test forktest
  char *forktest_args[] = {"forktest", 0};
  run_test("forktest", forktest_args, 0, repeat); // Fixed tick interval
  run_test("forktest", forktest_args, 1, repeat); // Dynamic tick interval
  printf("\n");
  
  // Test ls
  char *ls_args[] = {"ls", 0};
  run_test("ls", ls_args, 0, repeat); // Fixed tick interval
  run_test("ls", ls_args, 1, repeat); // Dynamic tick interval
  printf("\n");
  
  // Test usertests (quick mode)
  char *usertests_args[] = {"usertests", "-q", 0};
  // run_test("usertests", usertests_args, 0, repeat); // Fixed tick interval
  run_test("usertests", usertests_args, 1, repeat); // Dynamic tick interval
  
  printf("\n=== Test Complete ===\n");
  exit(0);
}