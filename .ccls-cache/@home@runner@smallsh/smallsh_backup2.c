#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>

static size_t WORDS = 513;

char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub)
{
  char *str = *haystack;
  size_t haystack_len = strlen(*haystack);
  size_t const needle_len = strlen(needle), sub_len = strlen(sub);

  for(; (str = strstr(str, needle));) {
    ptrdiff_t off = str - *haystack;
    if ( sub_len > needle_len) {
      str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len + 1));
      if (!str) goto exit;
      *haystack = str;
      str = *haystack + off;
    }
  
    memmove(str + sub_len, str + needle_len, haystack_len + 1 - off - needle_len);
    memcpy(str, sub, sub_len);
    haystack_len = haystack_len + sub_len - needle_len;
    str += sub_len;
  }
  str = *haystack;
  if (sub_len < needle_len) {
    str = realloc(*haystack, sizeof **haystack *(haystack_len + 1));
    if (!str) goto exit;
    *haystack = str;
  }

exit:
  return str;
}

void handle_SIGINT(int signo){
	return;
}

void check_background(int bg_pid, int bg_proc_status, int *bg_counter){
  bg_pid = waitpid(0, &bg_proc_status, WNOHANG);
  if (WIFEXITED(bg_proc_status)!=0){
    fprintf(stderr, "Child process %jd done. Exit status %d\n", (intmax_t) bg_pid, WEXITSTATUS(bg_proc_status));
    fflush(stdout);
    *bg_counter = *bg_counter-1;
  }
  if (WIFSIGNALED(bg_proc_status)!=0){
    fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) bg_pid, WTERMSIG(bg_proc_status)); 
  }
exit:
  return;
}

// gets user input line and splits into words
void get_and_split_input(char *input_array[], struct sigaction SIGINT_action) {
  char *line = NULL;
  char *delimiter;
  char *token;
  size_t num_args = 0;
  //sets the delimiter variable based on IFS enviorment variable. If delimiter can't be set print error and exit
  if (getenv("IFS") == NULL){
    delimiter =  strdup(" \t\n");
  }
  else{
    delimiter = strdup(getenv("IFS"));
  }
  if (!delimiter) {
    fprintf(stderr, "Error: could not allocate memory for delimiter.\n");
    goto exit;
  }
  
  // print prompt string and tokenize user input
  if (getenv("PS1")) fprintf(stderr, "%s", getenv("PS1"));
  else fprintf(stderr,  "%s", "");
  fflush(stdout);

  // Install our signal handler
	sigaction(SIGINT, &SIGINT_action, NULL);
  
  ssize_t line_length = getline(&line, &num_args, stdin);

  //no line read, exit
  if (!line_length) goto exit;

  //tokenize the user input and duplicate each token into input_array
  token = strtok(line, delimiter);
  for(int i=0; token; i++){
    //if (*token == '#') goto exit;
    input_array[i] = strdup(token);
    if (!input_array[i]) {
      fprintf(stderr, "Error: could not allocate memory for input token.\n");
      goto exit;
    }
    token = strtok(NULL, delimiter);
  }

//free memory, rest errno and return
exit:  
  free(line);
  free(delimiter);
  errno=0;
  return;
}

//function to expand special characters
void expand_input(char *expand_array[], int *bg_pid, int *fg_proc_status) {
  //create strings to hold shell pid, fg exit status and bg pid for use in str_gsub
  char pid_str[20];
  char bg_pid_str[20];
  char fg_proc_status_str[20];
  sprintf(pid_str, "%d", getpid());
  
  // set $? and $! env variables if not set 
  if (*bg_pid == -1){
    bg_pid_str[0] = '\0';
    }
  else {
    sprintf(bg_pid_str, "%d", *bg_pid);
  }
  if (*fg_proc_status == -1) sprintf(fg_proc_status_str, "%d", 0);  
  else {
    fg_proc_status_str[0] = '\0';
    sprintf(fg_proc_status_str, "%d", *fg_proc_status);  
  }
  
  //loops through letters in each word and looks for special characters that need to be expanded 
  for (int i = 0; expand_array[i]; i++){
    char home[512];
    home[0] = '\0';
    strcat(home, getenv("HOME"));
    strcat(home, "/");

    for (int j = 0; expand_array[i][j]; j++){
      if (expand_array[i][0] == '~' && expand_array[i][1] == '/'){
        str_gsub(&expand_array[i], "~/", home);
      }
      else if  (expand_array[i][j] == '$' && expand_array[i][j+1] == '$'){
        str_gsub(&expand_array[i], "$$", pid_str);
      }
      //need to update after completing child process stuff  
      else if  (expand_array[i][j] == '$' && expand_array[i][j+1] == '?'){
        
        str_gsub(&expand_array[i], "$?",  fg_proc_status_str);
        
      }
      else if  (expand_array[i][j] == '$' && expand_array[i][j+1] == '!'){ 
        str_gsub(&expand_array[i], "$!", bg_pid_str);
      }
    }
  }
exit:
  return;
}

//function to parse the user's input and expand special characters
void parse_input(char *parse_array[], int *bg_proc, char input_file[], char output_file[]) {
  for (int i = 0; parse_array[i]; i++){
    if (!parse_array[i+1] || *parse_array[i] == '#'){
      if (*parse_array[i] == '#') {
        parse_array[i] = NULL;
        i--;
      }
      if (*parse_array[i] == '&') {
        parse_array[i] = NULL;
        i--;
        *bg_proc = 1;
      }
      for ( int j = 0; j < 2; j++)
        if (i > 0 && (*parse_array[i-1] == '<' || *parse_array[i-1] == '>')){
          if (*parse_array[i-1] == '>') {
            strcpy(output_file, parse_array[i]);
            parse_array[i] = NULL;
            parse_array[i-1] = NULL;
            i = i-2;
          }
          else if (*parse_array[i-1] == '<') {
            strcpy(input_file, parse_array[i]);
            parse_array[i] = NULL;
            parse_array[i-1] = NULL;
            i = i-2;
          }
        }
    }
  }
  
exit:
  errno = 0;  
  return;
}

void exec_input(char *exec_array[], int *bg_proc, char input_file[], char output_file[], int *bg_pid, int *bg_proc_status, int *fg_proc_status, int *bg_counter) {
  
  if (strcmp(exec_array[0], "exit") == 0){
    if (exec_array[2]){
      fprintf(stderr, "Error: Too many arguments provided.\n");
      goto exit;
    }

    else if (exec_array[1] && !isdigit(*exec_array[1])){
      fprintf(stderr, "Error: Ivalid exit status\n");
      goto exit;
    }
        
    else {
      fprintf(stderr, "\nexit\n");
      kill(0, SIGINT);
      if (!exec_array[1]) exit(*fg_proc_status);
      else exit(atoi(exec_array[1]));
    }
  }
    
  else if (strcmp(exec_array[0], "cd") == 0){
    if (exec_array[2]){
      fprintf(stderr, "Error: Too many arguments provided.\n");
      goto exit;
    }
    else if (exec_array[1]){
      if (chdir(exec_array[1])== -1) fprintf(stderr, "Error: change dirrectory failed\n");
    }
    else{
      if (chdir(getenv("HOME")) == -1) fprintf(stderr, "Error: change dirrectory failed\n");
    } 
  }

  else{
    int child_status;
    // Fork a new process
    pid_t child_pid = fork();
    
    switch(child_pid){
    case -1:
      perror("fork()\n");
      exit(1);
      break;
      
    case 0:
      // if input or output file is provided, redirect accordingly
      if (strlen(input_file) > 0) {
        int sourceFD = open(input_file, O_RDONLY);
        if (sourceFD == -1) {
          fprintf(stderr, "Error: could not open file to input.\n");
          exit(1);
        }
        int input = dup2(sourceFD, 0);
        if (input == -1) {
          fprintf(stderr, "Error: could not redirect file to input.\n");
          exit(1);
        }
      }
      if (strlen(output_file) > 0) {
        int target_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (target_fd == -1) {
          fprintf(stderr, "Error: could not open file to output.\n");
          exit(1);
        }
        int output = dup2(target_fd, 1);
        if (output == -1) {
          fprintf(stderr, "Error: could not redirect file to output.\n");
          exit(1);
        }
      }
		  // In the child process
		  execvp(exec_array[0], exec_array);
		  perror("execve");
		  exit(2);
		  break;
      
    default:
      
      // if bg_proc is set, run child process in the background and set $! variable with child's pid
      if (*bg_proc == 1){
        *bg_pid = child_pid;
        child_pid = waitpid(child_pid, &child_status, WNOHANG);
        *bg_counter = *bg_counter+1;
      }
      // else wait for child process and update $? with exit status of the child
		  else {
        child_pid = waitpid(child_pid, &child_status, 0);
        char spawn_pid_str[20];
        if(WIFEXITED(child_status)){
          *fg_proc_status = WEXITSTATUS(child_status);
        }
        else {
          *fg_proc_status = 128 + WTERMSIG(child_status);
        }

      if (WIFSTOPPED(child_status)) {
        child_pid = waitpid(child_pid, &child_status, 0);
        fprintf(stderr, "Child process %d stopped. Continuing.\n", child_pid);
        *bg_pid = child_pid;
        kill(child_pid, SIGCONT);
      }
      }
      
      break;
    } 
  }
exit:
  return;
}

int main(void) {
  // initialize variables for storing user input
  char *input[WORDS];
  int bg_proc = 0;
  char input_file[512] = {'\0'};
	char output_file[512] = {'\0'};
  for (int i=0; i<WORDS; i++) {
		input[i] = NULL;
	}
  int bg_counter = 0;
  int bg_proc_status = -1;
  int bg_pid = -1;
  int fg_proc_status = -1;

  // signal handler for sig int
  struct sigaction SIGINT_action = {0};
	SIGINT_action.sa_handler = handle_SIGINT;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;

  //signal handler for sigtstp
  struct sigaction SIGTSTP_action = {0};
	SIGINT_action.sa_handler = SIG_IGN;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = 0;
  sigaction(SIGTSTP, &SIGTSTP_action, NULL);

  
	for(;;) {
    // check if there are processes being run in the background and print notification for any that have finished
    if (bg_counter > 0){
      check_background(bg_pid, bg_proc_status, &bg_counter);
    }
		// get user input arguments and split into words
		get_and_split_input(input, SIGINT_action);
    // expand special characters within input
    expand_input(input, &bg_pid, &fg_proc_status);
    // parse input and check for file redirection
    parse_input(input, &bg_proc, input_file, output_file);
    // if input is given execute the commands
    if (*input){
      exec_input(input, &bg_proc, input_file, output_file, &bg_pid, &bg_proc_status, &fg_proc_status, &bg_counter);
    }
    // reset the input array to null
    for (int i=0; i<512; i++) {
		  input[i] = NULL;
      }
    bg_proc = 0;
    input_file[0] = '\0';
    output_file[0] = '\0';
	}
  free(*input);
	return 0;
}
