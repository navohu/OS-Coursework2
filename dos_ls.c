/* 3005 coursework 2 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"


void print_indent(int indent)
{
  int i;
  for (i = 0; i < indent; i++)
    printf(" ");
}

/* Recursively goes through each directory on the FAT12 image file and lists all files and directories */
void follow_dir(uint16_t cluster, int indent, uint8_t *image_buf, struct bpb33* bpb)
{

    struct direntry *dirent;
    int d, i;
    dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb); //returns the address to the cluster
    while (1) {
        //for less than bytesPerSec*seconPerClust and increase with size of direntry at a time (32)
        for (d = 0; d < bpb->bpbBytesPerSec * bpb->bpbSecPerClust; d += sizeof(struct direntry)) { 
            //File information
            char name[9]; 
            char extension[4];
            uint32_t size;
            uint16_t file_cluster;
            name[8] = ' ';
            extension[3] = ' ';
            memcpy(name, &(dirent->deName[0]), 8); //creates a max space of 8 characters in a file
            memcpy(extension, dirent->deExtension, 3); //length 3 in extension
            if (name[0] == SLOT_EMPTY)
            return;

            /* skip over deleted entries */
            if (((uint8_t)name[0]) == SLOT_DELETED)
            continue;

            /* names are space padded - remove the spaces and add null character */
            for (i = 8; i > 0; i--) {
                if (name[i] == ' ') 
                name[i] = '\0';
            else 
                break;
            }

            /* remove the spaces from extensions */
            for (i = 3; i > 0; i--) {
                if (extension[i] == ' ') 
                extension[i] = '\0';
            else 
                break;
            }

            /* don't print "." or ".." directories */
            if (strcmp(name, ".")==0) {
                dirent++;
                continue;
            }
            if (strcmp(name, "..")==0) {
                dirent++;
                continue;
            }

            if ((dirent->deAttributes & ATTR_VOLUME) != 0) {            //If the dirent show volume
                printf("Volume: %s\n", name);
            } else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {  //If current dirent is a directory
                print_indent(indent);
                printf("%s (directory)\n", name);
                file_cluster = getushort(dirent->deStartCluster);
                follow_dir(file_cluster, indent+2, image_buf, bpb);
            } else {                                                    //If it is a file
                size = getulong(dirent->deFileSize);
                print_indent(indent);
                printf("%s.%s (%u bytes)\n", name, extension, size);
            }
            dirent++; //Go to next directory entry
        }
        if (cluster == 0) {
            // root dir is special
            dirent++;
        } else {
            cluster = get_fat_entry(cluster, image_buf, bpb);
            dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
        }
    }
}

void usage()
{
    fprintf(stderr, "Usage: dos_ls <imagename>\n");
    exit(1);
}



int main(int argc, char** argv)
{
    uint8_t *image_buf;
    int fd;
    struct bpb33* bpb;
    if (argc < 2 || argc > 2) {
    usage();
    }

    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf); // returns a bpb33 struct

    follow_dir(0, 0, image_buf, bpb);
    close(fd);
    exit(0);
}
