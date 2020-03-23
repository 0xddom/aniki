/* 
 * DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                   Version 2, December 2004
 
Copyright (C) 2004 Sam Hocevar <sam@hocevar.net>

Everyone is permitted to copy and distribute verbatim or modified
copies of this license document, and changing it is allowed as long
as the name is changed.
 
           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
  TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

 0. You just DO WHAT THE FUCK YOU WANT TO.

   Get the dylibs loaded by a macho file.

   RIP Billy Herrington.
 *
 * Reference: http://www.stonedcoder.org/~kd/lib/MachORuntime.pdf
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define FAT_CIGAM 0xbebafeca
#define MH_MAGIC 0xfeedface
#define MH_MAGIC_64 0xfeedfacf

#if HAVE_BYTESWAP_H
#    include <byteswap.h>
#else
#    define bswap_16(value) \
((((value) & 0xff) << 8) | ((value) >> 8))

#    define bswap_32(value) \
(((uint32_t)bswap_16((uint16_t)((value) & 0xffff)) << 16) | \
(uint32_t)bswap_16((uint16_t)((value) >> 16)))

#    define bswap_64(value) \
(((uint64_t)bswap_32((uint32_t)((value) & 0xffffffff)) \
<< 32) | \
(uint64_t)bswap_32((uint32_t)((value) >> 32)))
#endif

typedef struct dylib_command DylibCommand;
typedef struct fat_header FatHeader;
typedef struct fat_arch FatArch;
typedef struct mach_header MachHeader;
typedef struct mach_header_64 MachHeader64;
typedef struct load_command LoadCommand;

void extract_from_fat (char *, size_t);
void extract_from_mach (char *, size_t);
void extract_from_mach_64 (char *, size_t);
void extract_commands (char *, size_t, uint32_t);

typedef struct {
  uint32_t magic;
  void (*handler)(char *, size_t);
} FileTypeHandler;

static FileTypeHandler handlers [] = {
  { FAT_CIGAM, extract_from_fat },
  { MH_MAGIC, extract_from_mach },
  { MH_MAGIC_64, extract_from_mach_64 },
  { 0, NULL }
};

void extract(char *, size_t);

int main(int argc, char **argv) {

  int fd;
  uint32_t magic_number;
  char *file_buffer;

  if (argc < 2) {
    fprintf (stderr, "[USAGE] %s <file>\n", argv[0]);
    exit (1);
  }

  if ((fd = open (argv[1], O_RDONLY)) < 0) {
    fprintf (stderr, "Can't open file: %s\n", strerror (errno));
    exit (errno);
  }

  struct stat st;
  fstat(fd, &st);
  size_t size = st.st_size;

  file_buffer = mmap (NULL, size, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0);
  if (file_buffer == MAP_FAILED) {
    fprintf (stderr, "Can't map the file in memory: %s\n", strerror (errno));
    exit (errno);
  }

  extract (file_buffer, size);

  munmap (file_buffer, size);
  close (fd);
  
  return 0;
}

void extract(char *buffer, size_t size) {
  int magic_number;
  magic_number = *((int *)buffer);
  int i;
  
  printf ("magic_number=0x%08x\n", magic_number);

  for (i = 0; handlers[i].magic != 0; i++) 
    if (handlers[i].magic == magic_number) 
      return handlers[i].handler (buffer, size);
  fprintf (stderr, "Magic number not recognized: 0x%08x\n", magic_number);
  exit (1);
}

void extract_from_fat(char *buffer, size_t size) {
  char *p;
  FatHeader header;
  int i;

  p = buffer;
  header = *(FatHeader *)buffer;
  header.nfat_arch = bswap_32 (header.nfat_arch);
  
  for (i = 0, p += sizeof (FatHeader); i < header.nfat_arch; i++, p += sizeof (FatArch)) {
    FatArch arch;

    arch = *(FatArch *)p;
    printf ("cputype=0x%08x\n"
            "cpusubtype=0x%08x\n"
            "offset=0x%08x (%d)\n"
            "size=%d\n"
            "align=0x%08x\n",
            arch.cputype,
            arch.cpusubtype,
            bswap_32 (arch.offset),
            bswap_32 (arch.offset),
            bswap_32 (arch.size),
            arch.align);
    extract (buffer + bswap_32 (arch.offset), bswap_32 (arch.size));
  }
}

void extract_from_mach(char *buffer, size_t size) {
  MachHeader header;

  header = *(MachHeader *)buffer;
  printf ("ncmds=%d\n"
          "sizeofcmds=%d\n",
          header.ncmds,
          header.sizeofcmds);

  extract_commands (buffer + sizeof (MachHeader), header.sizeofcmds, header.ncmds);
}

void extract_from_mach_64(char *buffer, size_t size) {
  char *p;
  MachHeader64 header;

  header = *(MachHeader64 *)buffer;
  /*printf ("ncmds=%d\n"
          "sizeofcmds=%d\n",
          header.ncmds,
          header.sizeofcmds);*/

  extract_commands (buffer + sizeof (MachHeader64), header.sizeofcmds, header.ncmds);
}

void extract_commands(char *buffer, size_t size, uint32_t ncmds) {
  int i;
  char *p;
  LoadCommand cmd;
  DylibCommand dylibcmd;
  char *dylibname;
  
  p = buffer;
  //printf ("LC_LOAD_DYLIB=0x%08x\n", LC_LOAD_DYLIB);
  for (i = 0; i < ncmds; i++) {
    cmd = *(LoadCommand *)p;
    switch (cmd.cmd) {
      case LC_LOAD_DYLIB:
      case LC_LOAD_WEAK_DYLIB:
      case LC_ID_DYLIB:
        dylibcmd = *(DylibCommand *)p;
        dylibname = p + dylibcmd.dylib.name.offset;

        printf ("library:%s\n", dylibname);
    }

    p += cmd.cmdsize;    
  }
}


