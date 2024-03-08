#include "wut.h"
#include <assert.h>     // assert
#include <errno.h>      // errno
#include <stddef.h>     // NULL
#include <stdio.h>      // perror
#include <stdlib.h>     // reallocarray
#include <sys/mman.h>   // mmap, munmap
#include <sys/queue.h>  // TAILQ_*
#include <sys/signal.h> // SIGSTKSZ
#include <sys/types.h>
#include <sys/ucontext.h>
#include <ucontext.h> // getcontext, makecontext, setcontext, swapcontext
#include <valgrind/valgrind.h> // VALGRIND_STACK_REGISTER


double load_factor = 0.9;


struct queue_entry {
  int id;
  TAILQ_ENTRY(queue_entry) pointers;
};

TAILQ_HEAD(queue_head, queue_entry);

// A FIFO Queue that keeps track of runnable threads
struct queue_head queue_head;


struct TCB {
  char *stack;
  ucontext_t *context;
  struct queue_entry entry;
  void (*pointer)(void);
  int id;
  int run;
  int available;
  int blocker_id;
  int blocked_id;
  int status;
};

// A thread control block pointer that keeps track of various
// information about threads
struct TCB *tcb;

// The size of the tcb array to be implmented
int size = 256;

// The number of lots that have been used at least once in tcb
// array
int count = 0;

//print error messages and exit
static void die(const char *message) {
  int err = errno;
  perror(message);
  exit(err);
}

//allocate a new stack on heap
static char *new_stack(void) {
  char *stack = mmap(NULL, SIGSTKSZ, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (stack == MAP_FAILED) {
    die("mmap stack failed");
  }
  VALGRIND_STACK_REGISTER(stack, stack + SIGSTKSZ);
  return stack;
}

//check the number of used threads in tcb
//and re-size the array if necessary
void check_and_resize_tcb() {
  if (count > size * load_factor) {
    size = size << 2;
    tcb = reallocarray(tcb, size, sizeof(struct TCB));
    if (tcb == NULL) {
      die("tcb re-alloc failed\n");
    }
  }
}

//check if a thread assosciated with the
//id is in the FIFO queue
int id_in_ready_queue(int id) {
  struct queue_entry *entry;
  if (TAILQ_EMPTY(&queue_head)) {
    return 0;
  }

  if (queue_head.tqh_first == *(queue_head.tqh_last)) {
    if (queue_head.tqh_first->id != id)
      return 0;
  }

  TAILQ_FOREACH(entry, &queue_head, pointers) {
    if (entry->id == id)
      return 1;
  }
  return 0;
}


//Delete a stack
static void delete_stack(char *stack) {
  if (munmap(stack, SIGSTKSZ) == -1) {
    die("munmap stack failed");
  }
}

//run the function passed in
//to the current running thread
//in FIFO queue and call wut_eixt
//if it has been called
void run_and_switch() {
  int current_id = wut_id();
  tcb[current_id].pointer();
  if (tcb[current_id].status == -1) {
    wut_exit(0);
  }
  exit(0);
}

//get the lowest available id
//for use in wut_create
int get_lowest_available_id() {
  for (int i = 0; i < count; i++) {
    if (tcb[i].available == 1)
      return i;
  }
  return count;
}

//tcb initialization 
void tcb_init(int id, int run, void (*pointer)(void)) {
  tcb[id].id = id;
  tcb[id].stack = new_stack();
  tcb[id].context = malloc(sizeof(ucontext_t));
  tcb[id].context->uc_stack.ss_sp = tcb[id].stack;
  tcb[id].context->uc_stack.ss_size = SIGSTKSZ;
  tcb[id].pointer = pointer;
  tcb[id].run = run;
  tcb[id].entry.id = id;
  tcb[id].available = 0;
  tcb[id].status = -1;
  tcb[id].blocker_id = -1;
  tcb[id].blocked_id = -1;
  if (id == count) {
    count++;
  }
}

//clean up a thread when it exits and gets acknowledgaed
void tcb_cleanup(int id, int cancelled){
  tcb[id].available = 1;
  tcb[id].run = 0;
  tcb[id].pointer = NULL;
  if(cancelled == 1) return;
  delete_stack(tcb[id].stack);
  free(tcb[id].context);
}

//libarary initialization. We set main thread as thread 0
//in tcb and allocate the tcb array on heap and setup the context
//of thread 0
void wut_init() {
  TAILQ_INIT(&queue_head);
  assert(TAILQ_EMPTY(&queue_head));
  tcb = reallocarray(tcb, size, sizeof(struct TCB));
  assert(tcb != NULL);
  tcb_init(0, 1, NULL);
  getcontext(tcb[0].context);
  makecontext(tcb[0].context, run_and_switch, 0);
}

//get the current running id in the FIFO queue
int wut_id() {
  for (int i = 0; i < count; i++) {
    if (tcb[i].run == 1)
      return i;
  }
  die("error: no thread is running in FIFO Queue\n");
  return -1;
}

//create a thread with the lowest available id
//and add it to the back of the FIFO queue
//this function returns the id of thread created
int wut_create(void (*run)(void)) {
  check_and_resize_tcb();
  int id = get_lowest_available_id();
  tcb_init(id, 0, run);
  TAILQ_INSERT_TAIL(&queue_head, &tcb[id].entry, pointers);
  getcontext(tcb[id].context);
  makecontext(tcb[id].context, run_and_switch, 0);
  return id;
}

//cancel a thread with the specified id if the thread
//of the id is not terminated
int wut_cancel(int id) {
  if (id == wut_id())
    return -1;
  if (id >= count)
    return -1;
  if (tcb[id].status != -1) {
    return -1;
  }
  if (id_in_ready_queue(id)) {
    TAILQ_REMOVE(&queue_head, &tcb[id].entry, pointers);
  }

  if (tcb[id].blocker_id != -1) {
    int block_id = tcb[id].blocker_id;
    TAILQ_INSERT_TAIL(&queue_head, &tcb[block_id].entry, pointers);
  }

  delete_stack(tcb[id].stack);
  free(tcb[id].context);
  tcb[id].status = 128;
  
  return 0;
}

// wait for the thread of joined id 
// to terminate and release all of its
// resources and mkae it available for 
// new threads. The waiting thread will
// not be added to the back of the queue
// unless the waited on thread has termianted
// after keeping track of necessary info for
// join function. control goes to next thread in
// the FIFO queue
int wut_join(int id) {
  if (id >= count)
    return -1;
  if (id == wut_id())
    return -1;
  if (tcb[id].blocker_id != -1)
    return -1;

  int curr_id = wut_id();

  if (tcb[id].status == 128) {
    if (tcb[id].blocked_id != -1) {
      tcb[tcb[id].blocked_id].blocker_id = -1;
    }
    tcb_cleanup(id, 1);
    return tcb[id].status;
  }
  else if (tcb[id].status > -1) {
    tcb_cleanup(id, 0);
    return tcb[id].status;
  }

  if (tcb[queue_head.tqh_first->id].run == 1) {
    TAILQ_REMOVE(&queue_head, queue_head.tqh_first, pointers);
  }

  tcb[curr_id].run = 0;
  tcb[id].blocker_id = curr_id;
  tcb[curr_id].blocked_id = id;

  int next_id = TAILQ_FIRST(&queue_head)->id;
  tcb[next_id].run = 1;
  TAILQ_REMOVE(&queue_head, queue_head.tqh_first, pointers);
  swapcontext(tcb[curr_id].context, tcb[next_id].context);
  if (TAILQ_EMPTY(&queue_head)) {
    tcb[curr_id].run = 1;
  }
  TAILQ_INSERT_TAIL(&queue_head, &tcb[curr_id].entry, pointers);
  if (tcb[id].status == 128) {
    tcb_cleanup(id, 1);
  }
  else if (tcb[id].status > -1) {
    tcb_cleanup(id, 0);
  }

  return tcb[id].status;
}

//yield to next thread in the FIFO queue
//and add the current running one to the
//back of the queue
int wut_yield() {
  if (TAILQ_EMPTY(&queue_head)) {
    return -1;
  }

  int curr_id = wut_id();

  tcb[curr_id].run = 0;

  TAILQ_INSERT_TAIL(&queue_head, &tcb[curr_id].entry, pointers);
  int next_id = TAILQ_FIRST(&queue_head)->id;
  tcb[next_id].run = 1;
  TAILQ_REMOVE(&queue_head, queue_head.tqh_first, pointers);
  swapcontext(tcb[curr_id].context, tcb[next_id].context);

  return 0;
}

// set the status of current running thread
// to a number specified by status. hand the
// control over to the next thread in the FIFO
// queue
void wut_exit(int status) {
  int id = wut_id(); 
  status &= 0xff;

  if (id_in_ready_queue(id)) {
    TAILQ_REMOVE(&queue_head, &tcb[id].entry, pointers);
  }

  tcb[id].status = status;
  tcb[id].run = 0;
  int front_id = -1000;

  if (tcb[id].blocker_id != -1) {
    int block_id = tcb[id].blocker_id;
    tcb[block_id].blocked_id = -1;
    TAILQ_INSERT_TAIL(&queue_head, &tcb[block_id].entry, pointers);
  }
  if (TAILQ_EMPTY(&queue_head)) {
    tcb[id].status = 0;
    exit(0);
  }

  front_id = TAILQ_FIRST(&queue_head)->id; // 0
  tcb[TAILQ_FIRST(&queue_head)->id].run = 1;
  TAILQ_REMOVE(&queue_head, queue_head.tqh_first, pointers);
  swapcontext(tcb[id].context, tcb[front_id].context);
}
