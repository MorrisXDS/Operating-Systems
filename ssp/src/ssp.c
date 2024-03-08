#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/procfs.h>
#include <sys/wait.h>
#include <unistd.h>

#define pid_max_length 16
#define max_path_length 256
#define running -1
#define capacity_threshold 0.75
#define pids_initial_capacity 256

// a process_array taht has an array that 
// holds the directly created child processes 
//pid, name and status, the process count
//and the process index and count
struct process_array subprocesses;
// a process_array taht has an array that
// holds the re-parented processes
// pid, name and status, the process count
// and the process index and count
struct process_array orphan_processes;

// an id for externl reference
int ssp_id = -1;
// the maximum length of names
// , used for formatting, is 3
// by default due to "CMD"
int max_name_length = 3;

//printout headers and names
const char PID[4] = "PID";
const char CMD[4] = "CMD";
const char STATUS[7] = "STATUS";
const char reaper_child[10] = "<unknown>";

// a process struct carries
// info like process id,
// process name and its status
struct process {
  pid_t pid;
  char *name;
  int status;
};

// a process_array struct taht has an array that
// holds processes pid, name and status, the 
// process count and the process index and count
struct process_array {
  struct process *child_pids;
  int process_count;
  int process_index;
  int capacity;
};

void process_array_init(struct process_array *array, int array_size){
  array->child_pids = malloc(array_size * sizeof(struct process));
  array->process_count = 0;
  array->process_index = -1;
  array->capacity = array_size;
}

void expand_process_array_capacity(struct process_array *array) {
  array->capacity *= 2;
  array->child_pids = reallocarray(array->child_pids, array->capacity,
                                 sizeof(struct process));
  if (array->child_pids == NULL) {
    perror("dynamically resizing failed");
    exit(1);
  }
}

void log_process_info(struct process_array *array, pid_t pid, int status, const char *name){
  // if reaching the capacity threshold, reallocate memory for the array
  if (array->process_count >= (array->capacity)*capacity_threshold) {
    expand_process_array_capacity(array);
  }
  // increment count and index to the correct position
  (array->process_index)++;
  (array->process_count)++;
  // storing pid, name and status for an orphaned process
  array->child_pids[array->process_index].pid = pid;
  array->child_pids[array->process_index].name = 
      malloc((strlen(name)+1) * sizeof(char));
  strcpy(array->child_pids[array->process_index].name,
         name);
  array->child_pids[array->process_index].status = status;

  // retrieve the length of the orph'd process's name
  int string_length =
      strlen(array->child_pids[array->process_index].name);

  // update the maximum allowed length of names if
  // the current name length is longer
  max_name_length =
      (max_name_length < string_length) ? string_length : max_name_length;
}

// a function that handles SIGCHLD
// It tells apart the immediate
// child processes and orphaned
// ones and process them accordingly
void Sigchld_handler(int signum) {
  // if not sigchld return
  if (signum != SIGCHLD)
    return;
  
  // default signal that means
  // the process with the signal 
  // is running
  int status = -1;
  // peek at the status of the process
  // to handle and return its pid
  int is_immediate_process;

  pid_t child_pid = 0;

  // call non-blocking waitpid until all processes who have changed status
  // are itereated over.
  while ((child_pid = waitpid(-1, &status, WNOHANG)) > 0)
  {
    // reset checking condition
    is_immediate_process = 0;
   
    // if its still running or there
    // is no child having exited, return
    if (status == -1 || child_pid == 0)
      return;
           
    // loop through the array of direct processes to
    // see if the signalled process matches aby of them
    // If answer is positive, recird status and return
    for (int count = 0; count < subprocesses.process_count; count++) {
      if (child_pid == subprocesses.child_pids[count].pid) {
        subprocesses.child_pids[count].status = status;
        is_immediate_process = 1;
        break;
      }
    }
 
    if (is_immediate_process) continue;

    // normalize the status
    status %= 255;

    // cap the orphan process number so that there is
    // no out-of-bound access
    log_process_info(&orphan_processes, child_pid, status, reaper_child);
  }
}

// a function that registers signal 
// and passes it to a handler
void register_signal(int signum) {
  struct sigaction child_seeker = {0};
  sigemptyset(&child_seeker.sa_mask);
  child_seeker.sa_handler = Sigchld_handler;
  // make sys calls restartable between signals
  // and not receive any non-terminated processes
  child_seeker.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  if (sigaction(signum, &child_seeker, NULL) == -1) {
    perror("sigaction");
    exit(errno);
  }
}

// init function
// must be called before any other functions
// in the library to ensure its functionality
void ssp_init() {
  // make our running process a
  // subreaper
  prctl(PR_SET_CHILD_SUBREAPER, 1);
  // initialized array on heap and other stuff on stack
  process_array_init(&subprocesses, pids_initial_capacity);
  process_array_init(&orphan_processes, pids_initial_capacity);
  // start monitoring SIGCHLD signals
  register_signal(SIGCHLD);
}

// a function that creates and logs a new process
int ssp_create(char *const *argv, int fd0, int fd1, int fd2) {
  // fork() generates a new sub process
  pid_t pid = fork();
  // in case fork fails
  assert(pid != -1);

  //parent process
  if (pid > 0) {
    // log info
    log_process_info(&subprocesses, pid, running,
                     argv[0]);
  }
  // child process
  else if(pid == 0){
    // path string creation for fd directory
    char *path = malloc(max_path_length * sizeof(char));
    snprintf(path, max_path_length, "/proc/%d/fd/", getpid());

    // open fd directory for read
    int fd = open(path, O_DIRECTORY | O_RDONLY);
    if (fd == -1) {
      perror("error has occured\n");
    }
    
    // get DIR_entry
    DIR *fd_directory = fdopendir(fd);
    assert(fd_directory != NULL);
    struct dirent * fd_entry;

    // now fd 0, 1, 2 point
    // to fd0, fd1, fd2 repsectively
    dup2(fd0, 0);
    dup2(fd1, 1);
    dup2(fd2, 2);

    // close all fd larger than 2
    while ((fd_entry = readdir(fd_directory)) != NULL) {
      if (fd_entry->d_type == DT_LNK) {
        int fd_to_close = atoi(fd_entry->d_name);
        if (fd_to_close > 2) {
          close(fd_to_close);
        }
      }
    }

    // done reading, clean up
    closedir(fd_directory);
    close(fd);
    free(path);

    // switch to passed in command
    int error_flag = execvp(argv[0], argv);
    // if failed, return with errno
    if (error_flag == -1) {
        exit(errno);
    }
  }

  // return ssp_id for external reference
  return ++ssp_id;
} 

// a function that returns the status of a direct child
int ssp_get_status(int ssp_id) {
  // a non-blocking waitpid call that checks the status of a imediate child subprocess
  waitpid(subprocesses.child_pids[ssp_id].pid, &subprocesses.child_pids[ssp_id].status, WNOHANG);
  return subprocesses.child_pids[ssp_id].status;
}

// a function that sends a signal to a child process
void ssp_send_signal(int ssp_id, int signum) {
  // if child dies, do nothing
  if (ssp_get_status(ssp_id) > -1) return;
  // sned the signal
  kill(subprocesses.child_pids[ssp_id].pid, signum);
}

// wait for all direct children to die
void ssp_wait() {
  // loop through all child processes before exiting
  for (int index = 0; index <= ssp_id; index++) {
    // blocking wait until a process exit
    waitpid(subprocesses.child_pids[index].pid,
            &subprocesses.child_pids[index].status, 0);

    // check if its a normal exit
    if(WIFEXITED(subprocesses.child_pids[index].status)){
      subprocesses.child_pids[index].status += 0;
    }
    // check if its a signal exit
    else if (WIFSIGNALED(subprocesses.child_pids[index].status)) {
      subprocesses.child_pids[index].status += 128;
    }
    // normalize status for sanity check
    subprocesses.child_pids[index].status %= 255;
  }
}

// a function that prinst all processes 
// taken care of by the subreaper
void ssp_print() {
  printf("%7s %*s %s\n",PID, -(max_name_length), CMD, STATUS);

  for (int count = 0; count < subprocesses.process_count; count++) {
    subprocesses.child_pids[count].status = ssp_get_status(count);
    printf("%7d %*s %d\n", subprocesses.child_pids[count].pid,
           -(max_name_length), subprocesses.child_pids[count].name,
           subprocesses.child_pids[count].status);
  }
  for (int count = 0; count < orphan_processes.process_count; count++) {
    printf("%7d %*s %d\n", orphan_processes.child_pids[count].pid,
           -(max_name_length), orphan_processes.child_pids[count].name,
           orphan_processes.child_pids[count].status);
  }
}
