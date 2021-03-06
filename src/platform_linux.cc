// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "node.h"
#include "platform.h"

#include <v8.h>

#include <sys/param.h> // for MAXPATHLEN

#ifndef ANDROID
#include <sys/sysctl.h>
#endif

#include <sys/sysinfo.h>
#include <unistd.h> // getpagesize, sysconf
#include <stdio.h> // sscanf, snprintf

/* SetProcessTitle */
#include <sys/prctl.h>
#include <linux/prctl.h>
#include <stdlib.h> // free
#include <string.h> // strdup

/* GetInterfaceAddresses */
#include <arpa/inet.h>
#include <sys/types.h>

#ifndef ANDROID
#include <ifaddrs.h>
#endif

#include <errno.h>
#include <sys/ioctl.h>
#include <net/if.h>

#if HAVE_MONOTONIC_CLOCK
#include <time.h>
#endif

namespace node {

using namespace v8;

static char buf[MAXPATHLEN + 1];
static char *process_title;
double Platform::prog_start_time = Platform::GetUptime();


char** Platform::SetupArgs(int argc, char *argv[]) {
  process_title = strdup(argv[0]);
  return argv;
}


void Platform::SetProcessTitle(char *title) {
#ifdef PR_SET_NAME
  if (process_title) free(process_title);
  process_title = strdup(title);
#ifdef ANDROID
  prctl(PR_SET_NAME, (unsigned long)process_title, 0, 0, 0);
#else
  prctl(PR_SET_NAME, process_title);
#endif
#else
  Local<Value> ex = Exception::Error(
    String::New("'process.title' is not writable on your system, sorry."));
  ThrowException(ex); // Safe, this method is only called from the main thread.
#endif
}


const char* Platform::GetProcessTitle(int *len) {
  if (process_title) {
    *len = strlen(process_title);
    return process_title;
  }
  *len = 0;
  return NULL;
}


int Platform::GetMemory(size_t *rss, size_t *vsize) {
  FILE *f = fopen("/proc/self/stat", "r");
  if (!f) return -1;

  int itmp;
  char ctmp;
  size_t page_size = getpagesize();
  char *cbuf;
  bool foundExeEnd;

  /* PID */
  if (fscanf(f, "%d ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* Exec file */
  cbuf = buf;
  foundExeEnd = false;
  if (fscanf (f, "%c", cbuf++) == 0) goto error; // (
  while (1) {
    if (fscanf(f, "%c", cbuf) == 0) goto error;
    if (*cbuf == ')') {
      foundExeEnd = true;
    } else if (foundExeEnd && *cbuf == ' ') {
      *cbuf = 0;
      break;
    }

    cbuf++;
  }
  /* State */
  if (fscanf (f, "%c ", &ctmp) == 0) goto error; /* coverity[secure_coding] */
  /* Parent process */
  if (fscanf (f, "%d ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* Process group */
  if (fscanf (f, "%d ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* Session id */
  if (fscanf (f, "%d ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* TTY */
  if (fscanf (f, "%d ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* TTY owner process group */
  if (fscanf (f, "%d ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* Flags */
  if (fscanf (f, "%u ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* Minor faults (no memory page) */
  if (fscanf (f, "%u ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* Minor faults, children */
  if (fscanf (f, "%u ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* Major faults (memory page faults) */
  if (fscanf (f, "%u ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* Major faults, children */
  if (fscanf (f, "%u ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* utime */
  if (fscanf (f, "%d ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* stime */
  if (fscanf (f, "%d ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* utime, children */
  if (fscanf (f, "%d ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* stime, children */
  if (fscanf (f, "%d ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* jiffies remaining in current time slice */
  if (fscanf (f, "%d ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* 'nice' value */
  if (fscanf (f, "%d ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* jiffies until next timeout */
  if (fscanf (f, "%u ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* jiffies until next SIGALRM */
  if (fscanf (f, "%u ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* start time (jiffies since system boot) */
  if (fscanf (f, "%d ", &itmp) == 0) goto error; /* coverity[secure_coding] */

  /* Virtual memory size */
  if (fscanf (f, "%u ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  *vsize = (size_t) itmp;

  /* Resident set size */
  if (fscanf (f, "%u ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  *rss = (size_t) itmp * page_size;

  /* rlim */
  if (fscanf (f, "%u ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* Start of text */
  if (fscanf (f, "%u ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* End of text */
  if (fscanf (f, "%u ", &itmp) == 0) goto error; /* coverity[secure_coding] */
  /* Start of stack */
  if (fscanf (f, "%u ", &itmp) == 0) goto error; /* coverity[secure_coding] */

  fclose (f);

  return 0;

error:
  fclose (f);
  return -1;
}


int Platform::GetExecutablePath(char* buffer, size_t* size) {
  *size = readlink("/proc/self/exe", buffer, *size - 1);
  if (*size <= 0) return -1;
  buffer[*size] = '\0';
  return 0;
}

int Platform::GetCPUInfo(Local<Array> *cpus) {
  HandleScope scope;
  Local<Object> cpuinfo;
  Local<Object> cputimes;
  unsigned int ticks = (unsigned int)sysconf(_SC_CLK_TCK),
               multiplier = ((uint64_t)1000L / ticks), cpuspeed;
  int numcpus = 0, i = 0;
  unsigned long long ticks_user, ticks_sys, ticks_idle, ticks_nice, ticks_intr;
  char line[512], speedPath[256], model[512];
  FILE *fpStat = fopen("/proc/stat", "r");
  FILE *fpModel = fopen("/proc/cpuinfo", "r");
  FILE *fpSpeed;

  if (fpModel) {
    while (fgets(line, 511, fpModel) != NULL) {
      if (strncmp(line, "model name", 10) == 0) {
        numcpus++;
        if (numcpus == 1) {
          char *p = strchr(line, ':') + 2;
          strcpy(model, p);
          model[strlen(model)-1] = 0;
        }
      } else if (strncmp(line, "cpu MHz", 7) == 0) {
        if (numcpus == 1) {
          sscanf(line, "%*s %*s : %u", &cpuspeed);
        }
      }
    }
    fclose(fpModel);
  }

  *cpus = Array::New(numcpus);

  if (fpStat) {
    while (fgets(line, 511, fpStat) != NULL) {
      if (strncmp(line, "cpu ", 4) == 0) {
        continue;
      } else if (strncmp(line, "cpu", 3) != 0) {
        break;
      }

      sscanf(line, "%*s %llu %llu %llu %llu %*llu %llu",
             &ticks_user, &ticks_nice, &ticks_sys, &ticks_idle, &ticks_intr);
      snprintf(speedPath, sizeof(speedPath),
               "/sys/devices/system/cpu/cpu%u/cpufreq/cpuinfo_max_freq", i);

      fpSpeed = fopen(speedPath, "r");

      if (fpSpeed) {
        if (fgets(line, 511, fpSpeed) != NULL) {
          sscanf(line, "%u", &cpuspeed);
          cpuspeed /= 1000;
        }
        fclose(fpSpeed);
      }

      cpuinfo = Object::New();
      cputimes = Object::New();
      cputimes->Set(String::New("user"), Number::New(ticks_user * multiplier));
      cputimes->Set(String::New("nice"), Number::New(ticks_nice * multiplier));
      cputimes->Set(String::New("sys"), Number::New(ticks_sys * multiplier));
      cputimes->Set(String::New("idle"), Number::New(ticks_idle * multiplier));
      cputimes->Set(String::New("irq"), Number::New(ticks_intr * multiplier));

      cpuinfo->Set(String::New("model"), String::New(model));
      cpuinfo->Set(String::New("speed"), Number::New(cpuspeed));

      cpuinfo->Set(String::New("times"), cputimes);
      (*cpus)->Set(i++, cpuinfo);
    }
    fclose(fpStat);
  }

  return 0;
}

double Platform::GetFreeMemory() {
  double pagesize = static_cast<double>(sysconf(_SC_PAGESIZE));
  double pages = static_cast<double>(sysconf(_SC_AVPHYS_PAGES));

  return static_cast<double>(pages * pagesize);
}

double Platform::GetTotalMemory() {
  double pagesize = static_cast<double>(sysconf(_SC_PAGESIZE));
  double pages = static_cast<double>(sysconf(_SC_PHYS_PAGES));

  return pages * pagesize;
}

double Platform::GetUptimeImpl() {
#if HAVE_MONOTONIC_CLOCK
  struct timespec now;
  if (0 == clock_gettime(CLOCK_MONOTONIC, &now)) {
    double uptime = now.tv_sec;
    uptime += (double)now.tv_nsec / 1000000000.0;
    return uptime;
  }
  return -1;
#else
  struct sysinfo info;
  if (sysinfo(&info) < 0) {
    return -1;
  }
  return static_cast<double>(info.uptime);
#endif
}

int Platform::GetLoadAvg(Local<Array> *loads) {
  struct sysinfo info;

  if (sysinfo(&info) < 0) {
    return -1;
  }
  (*loads)->Set(0, Number::New(static_cast<double>(info.loads[0]) / 65536.0));
  (*loads)->Set(1, Number::New(static_cast<double>(info.loads[1]) / 65536.0));
  (*loads)->Set(2, Number::New(static_cast<double>(info.loads[2]) / 65536.0));

  return 0;
}

#ifndef ANDROID
bool IsInternal(struct ifaddrs* addr) {
  return addr->ifa_flags & IFF_UP &&
         addr->ifa_flags & IFF_RUNNING &&
         addr->ifa_flags & IFF_LOOPBACK;
}
#endif


Handle<Value> Platform::GetInterfaceAddresses() {
#ifdef ANDROID
  return v8::Undefined();
#else
  HandleScope scope;
  struct ::ifaddrs *addrs, *ent;
  struct ::sockaddr_in *in4;
  struct ::sockaddr_in6 *in6;
  char ip[INET6_ADDRSTRLEN];
  Local<Object> ret, o;
  Local<String> name, ipaddr, family;
  Local<Array> ifarr;

  if (getifaddrs(&addrs) != 0) {
    return ThrowException(ErrnoException(errno, "getifaddrs"));
  }

  ret = Object::New();

  for (ent = addrs; ent != NULL; ent = ent->ifa_next) {
    bzero(&ip, sizeof (ip));
    if (!(ent->ifa_flags & IFF_UP && ent->ifa_flags & IFF_RUNNING)) {
      continue;
    }

    if (ent->ifa_addr == NULL) {
      continue;
    }

    /*
     * On Linux getifaddrs returns information related to the raw underlying
     * devices. We're not interested in this information.
     */
    if (ent->ifa_addr->sa_family == PF_PACKET)
	    continue;

    name = String::New(ent->ifa_name);
    if (ret->Has(name)) {
      ifarr = Local<Array>::Cast(ret->Get(name));
    } else {
      ifarr = Array::New();
      ret->Set(name, ifarr);
    }

    if (ent->ifa_addr->sa_family == AF_INET6) {
      in6 = (struct sockaddr_in6 *)ent->ifa_addr;
      inet_ntop(AF_INET6, &(in6->sin6_addr), ip, INET6_ADDRSTRLEN);
      family = String::New("IPv6");
    } else if (ent->ifa_addr->sa_family == AF_INET) {
      in4 = (struct sockaddr_in *)ent->ifa_addr;
      inet_ntop(AF_INET, &(in4->sin_addr), ip, INET6_ADDRSTRLEN);
      family = String::New("IPv4");
    } else {
      (void) strncpy(ip, "<unknown sa family>", INET6_ADDRSTRLEN);
      family = String::New("<unknown>");
    }

    o = Object::New();
    o->Set(String::New("address"), String::New(ip));
    o->Set(String::New("family"), family);
    o->Set(String::New("internal"), ent->ifa_flags & IFF_LOOPBACK ?
	True() : False());

    ifarr->Set(ifarr->Length(), o);

  }

  freeifaddrs(addrs);

  return scope.Close(ret);
#endif
}

}  // namespace node
