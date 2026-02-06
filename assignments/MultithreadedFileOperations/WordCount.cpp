// WordCount.cpp
//
// Word count a set of files using a separate thread for each file.
//
// Compile with g++ --std=c++17 WordCount.cpp -pthread -o WordCount

#include <cctype>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace std;

// WordCount runs in a separate thread, counting words
// in an single file.
//
// If a file can't be opened, ignore it, adding zero words to
// the count.
//
// You are free to use either a memory-mapped implemenation
// or one that reads into a buffer.
//
// When you create a thread, you get to pass a single
// argument as a void *, which the child thread can caste to
// anything meaningful it likes, e.g., a char *pathname.
//
// When WordCount finishes counting a file, the result needs
// to be added to the total.  There are two ways to do this.
//
// (a) Add the count to a shared running total, locking
//     the resource with a pthread mutex while changing it.
//     (Most common.)
//
// (b) Caste the count to a void * and return it as its exit
//     status, which can be retrieved in main with pthread_join
//     and added there to the total.

int TotalWords = 0;

// YOUR CODE HERE
pthread_mutex_t m_mutex;

void *WordCount(void *arg) {
  int cnt = 0;
  // YOUR CODE HERE
  char *pathname = (char *)arg;
  // open the file
  int fd = open(pathname, O_RDONLY);
  if (fd == -1) {
    return nullptr;
  }
  struct stat fileInfo;
  if (fstat(fd, &fileInfo) == -1) {
    close(fd);
    return nullptr;
  }
  size_t fileSize = fileInfo.st_size;
  if (fileSize == 0) {
    close(fd);
    return nullptr;
  }
  const char *map =
      (const char *)mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);

  if (map == MAP_FAILED) {
    close(fd);
    return nullptr;
  }

  bool in_word = false;
  for (size_t i = 0; i < fileSize; ++i) {
    unsigned char c = static_cast<unsigned char>(map[i]);
    if (std::isspace(c)) {
      in_word = false;
    } else if (!in_word) {
      in_word = true;
      ++cnt;
    }
  }

  // modifying shared count
  pthread_mutex_lock(&m_mutex);
  TotalWords += cnt;
  pthread_mutex_unlock(&m_mutex);
  // Close the file
  munmap((void *)map, fileSize);
  close(fd);
  return nullptr;
}

// main() should iterate over the list of filenames given as
// arguments, creating a new thread running WordCount for each of the
// files.  Do not wait for a thread to finish before creating the
// next one.  Once all the threads complete, print total words.

int main(int argc, char **argv) {
  if (argc <= 1) {
    cerr << "Usage: WordCount <filenames>" << endl
         << "Count the total number of word in the files." << endl
         << "Invalid paths are ignored." << endl;
    return 1;
  }

  // YOUR CODE HERE
  pthread_mutex_init(&m_mutex, NULL); // init mutex
  pthread_t threads[argc - 1];
  for (size_t i = 0; i < argc - 1; i++) {
    pthread_t tid; // Variable to store the new thread's ID
    int ret;
    // Create the thread
    ret = pthread_create(&threads[i], NULL, WordCount, argv[i + 1]);
    if (ret != 0) {
      perror("pthread_create error");
      exit(EXIT_FAILURE);
    }
  }

  for (size_t i = 0; i < argc - 1; i++) {
    pthread_join(threads[i], NULL); // Wait for each thread
  }

  std::cout << "Total words = " << TotalWords << endl;
  pthread_mutex_destroy(&m_mutex);
  return 0;
}
