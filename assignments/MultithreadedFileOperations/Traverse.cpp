// Traverse.cpp
//
// Create and print a sorted list of all the paths specified
// on the command line, traversing any directories to add
// their contents using a pool of worker threads.  Ignore
// any non-existent paths.
//
// Compile with g++ --std=c++17 Traverse.cpp -pthread -o Traverse

#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

using namespace std;

//		YOUR CODE HERE

std::deque<std::string> work_queue;
std::vector<std::string> result_list;
pthread_mutex_t m_mutex;
pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
int sleeping_threads = 0;
int total_threads = 0;
bool done = false;

// Get the next path to be traversed from the work queue
// shared among all the threads, sleeping if the queue is
// empty. If this is the last thread going to sleep, signal
// that all the work is done.

string GetWork() {

  //		YOUR CODE HERE
  pthread_mutex_lock(&m_mutex);
  while (work_queue.empty() && !done) {
    sleeping_threads++;
    if (sleeping_threads == total_threads) {
      done = true;
      pthread_cond_broadcast(&cv);
      pthread_mutex_unlock(&m_mutex);
      return "";
    }
    pthread_cond_wait(&cv, &m_mutex);
    sleeping_threads--;
  }

  if (done && work_queue.empty()) {
    pthread_mutex_unlock(&m_mutex);
    return "";
  }

  string path = work_queue.front();
  work_queue.pop_front();
  pthread_mutex_unlock(&m_mutex);
  return path;
}

// Add  work to the queue and signal that there's work available.

void AddWork(string path) {

  //		YOUR CODE HERE
  pthread_mutex_lock(&m_mutex);
  work_queue.push_back(path);
  pthread_cond_signal(&cv);
  pthread_mutex_unlock(&m_mutex);
}

// Add a new path to the list all those that have been found,

void AddPath(string path) {

  //		YOUR CODE HERE
  pthread_mutex_lock(&m_mutex);
  result_list.push_back(path);
  pthread_mutex_unlock(&m_mutex);
}

bool DotName(const char *name) {
  return name[0] == '.' && (name[1] == 0 || name[1] == '.' && name[2] == 0);
}

// Traverse a path.  If it exists, add it to the list of paths that
// have been found.  If it's a directory, add any children other than
// . and .. to the work queue.  If it doesn't exist, ignore it.

void *Traverse(string pathname) {

  //		YOUR CODE HERE
  struct stat sb;
  if (lstat(pathname.c_str(), &sb) != 0) {
    return nullptr;
  }

  AddPath(pathname);

  if (!S_ISDIR(sb.st_mode)) {
    return nullptr;
  }

  DIR *dir = opendir(pathname.c_str());
  if (dir == nullptr) {
    return nullptr;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (DotName(entry->d_name)) {
      continue;
    }
    string child = pathname;
    if (!child.empty() && child.back() != '/') {
      child += "/";
    }
    child += entry->d_name;
    AddWork(child);
  }

  closedir(dir);
  return nullptr;
}

// Each worker thread simply loops, grabbing the next item
// on the work queue and traversing it.

void *WorkerThread(void *arg) {
  while (true) {
    string path = GetWork();
    if (path.empty()) {
      break;
    }
    Traverse(path);
  }

  // Never reached.
  return nullptr;
}

// main() should do the following.
//
// 1. Initialize the locks.
// 2. Iterate over the argv pathnames, adding them to the work queue.
// 3. Create the specified number of workers.
// 4. Sleep until the work has finished.
// 5. Sort the paths found vector.
// 6  Print the list of paths.

int main(int argc, char **argv) {
  int ThreadCount;
  if (argc < 3 || (ThreadCount = atoi(argv[1])) == 0) {
    cerr << "Usage: Traverse <number of workers> <list of pathnames>" << endl
         << "Number of workers must be greater than 0." << endl
         << "Invalid paths are ignored." << endl;
    return 1;
  }

  //		YOUR CODE HERE
  total_threads = ThreadCount;
  pthread_mutex_init(&m_mutex, NULL); // init mutex
  pthread_t worker_threads[ThreadCount];
  // pushing in the initial pathnames
  for (size_t i = 0; i < argc - 2; i++) {
    AddWork(argv[i + 2]);
  }

  // creating worker threads
  for (size_t i = 0; i < ThreadCount; i++) {
    if (pthread_create(&worker_threads[i], NULL, WorkerThread, (void *)i) !=
        0) {
      perror("Failed to create thread");
    }
  }

  // waiting worker threads to join
  for (size_t i = 0; i < ThreadCount; i++) {
    pthread_join(worker_threads[i], NULL);
  }

  sort(result_list.begin(), result_list.end());
  for (const auto &path : result_list) {
    cout << path << endl;
  }

  pthread_cond_destroy(&cv);
  pthread_mutex_destroy(&m_mutex);
  return 0;
}
