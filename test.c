#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <scsi/sg_cmds_basic.h>
#include <scsi/sg_cmds_extra.h>

#include <sys/statvfs.h>

int main(int argc, char* argv[]){

  const unsigned int GB = (1024 * 1024) * 1024;

  struct statvfs vfs;
  int statvfs_result = statvfs(argv[1], &vfs);
  
  printf("f_bsize (block size): %lu\n"
       "f_frsize (fragment size): %lu\n"
       "f_blocks (size of fs in f_frsize units): %lu\n"
       "f_bfree (free blocks): %lu\n"
       "f_bavail free blocks for unprivileged users): %lu\n"
       "f_files (inodes): %lu\n"
       "f_ffree (free inodes): %lu\n"
       "f_favail (free inodes for unprivileged users): %lu\n"
       "f_fsid (file system ID): %lu\n"
       "f_flag (mount flags): %lu\n"
       "f_namemax (maximum filename length)%lu\n",
       vfs.f_bsize,
       vfs.f_frsize,
       vfs.f_blocks,
       vfs.f_bfree,
       vfs.f_bavail,
       vfs.f_files,
       vfs.f_ffree,
       vfs.f_favail,
       vfs.f_fsid,
       vfs.f_flag,
       vfs.f_namemax);
/*
       unsigned long total = vfs.f_blocks * vfs.f_frsize /1024;
       unsigned long available = vfs.f_bavail * vfs.f_frsize / 1024;
       unsigned long free = vfs.f_bfree * vfs.f_frsize / 1024;
       unsigned long used = total - free;*/

       unsigned long total = vfs.f_blocks * vfs.f_frsize ;
       unsigned long available = vfs.f_bavail * vfs.f_frsize ;
       unsigned long free = vfs.f_bfree * vfs.f_frsize ;
       unsigned long used = total - free;

       const double totald = (double)(vfs.f_blocks * vfs.f_frsize) / GB;
       printf("Total: %f --> %.0f\n", totald, totald);
       
       printf("Total 2: %luK\n", total);
       printf("Available 2: %luK\n", available);
       printf("Used 2: %luK\n", used);
       printf("Free 2: %luK\n", free);

long long Total_Space = vfs.f_blocks;
Total_Space *= vfs.f_frsize;
Total_Space /= 1024;
long long Avail_Space = vfs.f_bfree;
Avail_Space *= vfs.f_frsize;
Avail_Space /= 1024;

printf("Total Space=%lldKb Available Space=%lldKB\n",Total_Space,Avail_Space);

       return 0;
    }