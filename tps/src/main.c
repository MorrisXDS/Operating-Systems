#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define max_size 128

// this function checks if a char is a digit
// returns 1 if it is. Otherwise, it returns 0
int is_a_digit(char MSB) { return MSB >= '0' && MSB <= '9'; }

int main() {
  //try to open the directory
  DIR *proc_dir = opendir("/proc/");

  //if the directory is not opened, there is sth wrong
  if (!proc_dir) {
    perror("directory was not opened properly");
    exit(1);
  }

  //create the header
  const char PID[4] = "PID";
  const char CMD[4] = "CMD";
  //print them out
  printf("%5s %s\n", PID, CMD);

  //a struct that holds info from dir stream entries
  struct dirent *process_entry;

  //query the stream until it reaches EOF (end of line)
  while ((process_entry = readdir(proc_dir)) != NULL) {
    // if the name (subpath) does not start with
    // a number. That suggests it is not a directory
    // in proc directory. we skip to next entry
    if (is_a_digit(process_entry->d_name[0]) != 1) continue;

      //complete the full path
      char process_dir[256] = "/proc/";
      strcat(process_dir, process_entry->d_name);
      strcat(process_dir, "/status");

      // try to open the file
      int fd = open(process_dir, O_RDONLY);

      // if there is an error opening the file, something's wrong
      // we skip to next stream entry
      if (fd == -1)
        continue;

      // prefix collects the prefix part "Name: " in a status file
      // name_buffer is used to read and buff the chars of a process's
      // name while the name char array stores the full name of the
      // process
      char prefix[6];
      char name_buffer[max_size];
      char name[max_size] = "";
      int bytes_read = max_size;

      // eat up the characters until the first char of name
      int read_flag = read(fd, prefix, 6 * sizeof(char));

      // if read fails, print an error message and exit.
      if (read_flag == -1) {
        perror("an error occured while reading the status file");
        exit(1);
      }

      //reads until EOF
      while (bytes_read != 0) {
        // if an error occurs while reading, print out a error
        // message and quit the whole program running
        if ((bytes_read = read(fd, name_buffer, 1*sizeof(char))) == -1) {
          perror("An error has occurred while reading the file");
          exit(1);
        }

        //add a char into the name array
        strcat(name, name_buffer);

        // when it reads \n espace character, the file descriptor
        // has reached the end of line, equivalently the end of
        // name string. thus ending the query 
        if (strcmp("\n", name_buffer) == 0) {
          break;
        }
      }

      // close the file after reading it
      // with error handling
      int status_file_close_flag = close(fd);
      if (status_file_close_flag != 0) {
        perror("An error has occurred while closing the file");
        exit(1);
      }

      //print out in the requried format
      printf("%5s ", process_entry->d_name);
      printf("%s", name);
      
    }

  //close the directory after finishing reading the dir stream
  int proc_dir_flag = closedir(proc_dir);

  // error handling
  if (proc_dir_flag == -1) {
    perror("An error has occurred while closing the proc directory");
    exit(1);
  }

  //give the control back to the kernel with return code 0  
  return 0;
}
