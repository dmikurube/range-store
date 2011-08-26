#include <cstdio>
#include <cstdlib>
#include <string>
#include <iterator>
#include <map>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <vector>

#define PAGE_SIZE 4096

#if defined(__LP64__) && !defined(OS_MACOSX)
typedef long int64;
#else
typedef long long int64;
#endif

#if defined(__LP64__) && !defined(OS_MACOSX)
typedef unsigned long uint64;
#else
typedef unsigned long long uint64;
#endif

struct MemoryInclusiveRange : public std::pair<uint64, uint64> {
  MemoryInclusiveRange(uint64 first, uint64 second)
      : std::pair<uint64, uint64>(first, second) {}
  bool operator<(const MemoryInclusiveRange& other) const {
    return second < other.first;
  }
};

template <typename A, typename B>
class MergedMap2 {
 public:
  class MergedIterator
      : public std::iterator<std::input_iterator_tag, MergedMap2<A, B>, void> {
    // ...
  };

  MergedMap2(std::map<MemoryInclusiveRange, A> map_a,
             std::map<MemoryInclusiveRange, B> map_b)
      : map_a_(map_a), map_b_(map_b) {
  }

  MergedIterator begin() {
    // ...
  }

  MergedIterator end() {
    // ...
  }

 private:
  std::map<MemoryInclusiveRange, A> map_a_;
  std::map<MemoryInclusiveRange, B> map_b_;
};

struct MapsEntry {
  uint64 first;
  uint64 last;
  unsigned char readable;
  unsigned char writable;
  unsigned char executable;
  unsigned char shared;
  uint64 offset;
  char device_major[3];
  char device_minor[3];
  unsigned int device_inode;
  std::string mapped;
};

typedef std::map<MemoryInclusiveRange, MapsEntry> maps_t;
typedef std::map<MemoryInclusiveRange, uint64> pagemap_t;
typedef std::map<MemoryInclusiveRange, uint64> mmap_t;
typedef std::map<MemoryInclusiveRange, uint64> malloc_t;

uint64 hex_to_uint64(char buf[], int begin, int end) {
  char backed_up = buf[end];
  buf[end] = '\0';
  uint64 value = strtoull(&buf[begin], NULL, 16);
  buf[end] = backed_up;
  return value;
}

uint64 dec_to_uint64(char buf[], int begin, int end) {
  char backed_up = buf[end];
  buf[end] = '\0';
  uint64 value = strtoull(&buf[begin], NULL, 10);
  buf[end] = backed_up;
  return value;
}

int read_maps_pagemap(
    FILE* maps_fp,
    int pagemap_fd,
    std::map<MemoryInclusiveRange, MapsEntry>* maps_ranges,
    std::map<MemoryInclusiveRange, uint64>* pagemap_ranges) {
  char buf[1024];
  while (true) {
    if (NULL == fgets(buf, sizeof(buf) - 1, maps_fp))
      break;
    MapsEntry entry;
    entry.first = hex_to_uint64(buf, 0, 16);
    entry.last = hex_to_uint64(buf, 17, 33);
    entry.readable = buf[34];
    entry.writable = buf[35];
    entry.executable = buf[36];
    entry.shared = buf[37];
    entry.offset = hex_to_uint64(buf, 39, 55);
    entry.device_major[0] = buf[56];
    entry.device_major[1] = buf[57];
    entry.device_major[2] = '\0';
    entry.device_minor[0] = buf[59];
    entry.device_minor[1] = buf[60];
    entry.device_minor[2] = '\0';
    entry.device_inode = dec_to_uint64(buf, 62, 72);

    int num_pages = (entry.last - entry.first) / PAGE_SIZE;

    if (pagemap_fd >= 0) {
      uint64 address = entry.first;
      long index = (entry.first / PAGE_SIZE) * sizeof(unsigned long long);
      long o = lseek64(pagemap_fd, index, SEEK_SET);
      if (o != index) {
        fprintf(stderr, "Seek failure");
        return -1;
      }
      for (int i = 0; i < num_pages; ++i) {
        unsigned long long pa;
        int t = read(pagemap_fd, &pa, sizeof(unsigned long long));
        if (t < 0) {
          fprintf(stderr, "Read failure");
          return -1;
        }
        (*pagemap_ranges)[MemoryInclusiveRange(
            entry.first + PAGE_SIZE * i,
            entry.first - 1 + PAGE_SIZE * (i+1))] = pa;
      }
    }

    if (NULL == fgets(buf, sizeof(buf) - 1, maps_fp))
      break;
    int buflen = strlen(buf);
    while (true) {
      if (buflen == 0) break;
      if (buf[buflen-1] == '\r') buf[--buflen] = '\0';
      else if (buf[buflen-1] == '\n') buf[--buflen] = '\0';
      else break;
    }
    if (buf[0] == '\0' || buf[0] == '\r' || buf[0] == '\n')
      entry.mapped = "";
    else
      entry.mapped = buf;

    (*maps_ranges)[MemoryInclusiveRange(entry.first, entry.last - 1)] = entry;
  }
  return 0;
}

#define MMAP ((unsigned char)0)
#define MALLOC ((unsigned char)1)

int read_malloc_mmap(
    FILE* objects_fp,
    std::map<MemoryInclusiveRange, uint64>* mmap_ranges,
    std::map<MemoryInclusiveRange, uint64>* malloc_ranges) {
  uint64 first;
  unsigned int size;
  unsigned char type;
  while (true) {
    if (1 != fread(&first, sizeof(uint64), 1, objects_fp))
      break;
    if (1 != fread(&size, sizeof(unsigned int), 1, objects_fp))
      break;
    if (1 != fread(&type, 1, 1, objects_fp))
      break;
    first = be64toh(first);
    size = be32toh(size);
    uint64 last = first - 1 + size;
    if (type == MMAP)
      (*mmap_ranges)[MemoryInclusiveRange(first, last)] = last - first + 1;
    else if (type == MALLOC)
      (*malloc_ranges)[MemoryInclusiveRange(first, last)] = last - first + 1;
  }
}

int main(int argc, char* argv[]) {
  if (argc < 3)
    return -1;

  FILE* maps_fp;
  if (strcmp(argv[1], "-") == 0)
    maps_fp = stdin;
  else if (NULL == (maps_fp = fopen(argv[1], "r")))
    return -1;

  int pagemap_fd;
  if (strcmp(argv[2], "-") == 0)
    pagemap_fd = -1;
  else if (0 > (pagemap_fd = open(argv[2], O_RDONLY)))
    return -1;

  std::map<MemoryInclusiveRange, MapsEntry> maps_ranges;
  std::map<MemoryInclusiveRange, uint64> pagemap_ranges;
  std::map<MemoryInclusiveRange, uint64> mmap_ranges;
  std::map<MemoryInclusiveRange, uint64> malloc_ranges;

  read_maps_pagemap(maps_fp, pagemap_fd, &maps_ranges, &pagemap_ranges);
  if (argc >= 4) {
    FILE* objects_fp;
    if (NULL != (objects_fp = fopen(argv[3], "r"))) {
      read_malloc_mmap(objects_fp, &mmap_ranges, &malloc_ranges);
      fclose(objects_fp);
    }
  }

  mmap_t::iterator mmap_i = mmap_ranges.begin();
  malloc_t::iterator malloc_i = malloc_ranges.begin();
  for (maps_t::iterator p = maps_ranges.begin();
       p != maps_ranges.end();
       ++p) {
    printf("%016llx - %016llx %c%c%c%c %x %2s:%2s %010d\n",
           p->first.first, p->first.second,
           p->second.readable,
           p->second.writable,
           p->second.executable,
           p->second.shared,
           p->second.offset,
           p->second.device_major,
           p->second.device_minor,
           p->second.device_inode);
    for (; mmap_i != mmap_ranges.end() &&
           mmap_i->first.second <= p->first.second;
         ++mmap_i) {
      printf("  > %016llx - %016llx (%llu) [mmap]\n",
             mmap_i->first.first, mmap_i->first.second,
             mmap_i->second);
    }
    for (; malloc_i != malloc_ranges.end() &&
           malloc_i->first.second <= p->first.second;
         ++malloc_i) {
      printf("  > %016llx - %016llx (%llu) [malloc]\n",
             malloc_i->first.first, malloc_i->first.second,
             malloc_i->second);
    }
    if (pagemap_fd >= 0) {
      for (uint64 first = p->first.first;
           first < p->first.second;
           first += PAGE_SIZE) {
        printf("    %016llx\n",
            pagemap_ranges[MemoryInclusiveRange(first, first + PAGE_SIZE - 1)]);
      }
    }
  }

  /*
  for (mmap_t::iterator p = mmap_ranges.begin();
       p != mmap_ranges.end();
       ++p) {
    printf("%016llx - %016llx (%llu) [mmap]\n",
           p->first.first, p->first.second,
           p->second);
  }

  for (malloc_t::iterator p = malloc_ranges.begin();
       p != malloc_ranges.end();
       ++p) {
    printf("%016llx - %016llx (%llu) [malloc]\n",
           p->first.first, p->first.second,
           p->second);
  }
  */

  close(pagemap_fd);
  fclose(maps_fp);

  return 0;
}
