/* Try to write to the code segment using a system call.
   The process must be terminated with -1 exit code. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  void *ret;
  int handle;
  // __asm __volatile("mov %%rsp, %%rax\n":"=a" (ret):: "cc", "memory");
  // msg("\nuser_program: %p\n", ret);
  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");
  read (handle, (void *) test_main, 1);
  // msg("\nuser_program_read: %p\n", &test_main);
  // fail ("survived reading data into code segment");
}
