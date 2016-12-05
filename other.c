#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"

uint32_t get_file_length(uint16_t cluster, uint8_t *image_buf, struct bpb33* bpb)
{
    uint32_t length = 1;

    cluster = get_fat_entry(cluster, image_buf, bpb);
    while (!is_end_of_file(cluster)) {
        cluster = get_fat_entry(cluster, image_buf, bpb);
        length++;
    }

    return length;
}

void mark_file_clusters_used(int usedClusters[], uint16_t cluster, uint32_t bytes_remaining, uint8_t *image_buf, struct bpb33* bpb)
{
    usedClusters[cluster] = 1;

    int clust_size = bpb->bpbSecPerClust * bpb->bpbBytesPerSec;
    int total_clusters = bpb->bpbSectors / bpb->bpbSecPerClust;

    if (cluster == 0) {
        fprintf(stderr, "Bad file termination\n");
        return;
    } else if (cluster > total_clusters) {
        abort(); /* this shouldn't be able to happen */
    }

    uint16_t next_cluster = get_fat_entry(cluster, image_buf, bpb);

    if (is_end_of_file(next_cluster)) {
        return;
    } else {
        mark_file_clusters_used(usedClusters, get_fat_entry(cluster, image_buf, bpb), bytes_remaining - clust_size, image_buf, bpb);
    }
}

void check_lost_files(int usedClusters[], uint16_t cluster, uint8_t *image_buf, struct bpb33* bpb)
{
    // A value of 1 means that the cluster is used somewhere.
    usedClusters[cluster] = 1;

    struct direntry *dirent;
    int d, i;
    dirent = (struct direntry*) cluster_to_addr(cluster, image_buf, bpb);
    int clust_size = bpb->bpbBytesPerSec * bpb->bpbSecPerClust;

    while (1) {
        for (d = 0; d < clust_size; d += sizeof(struct direntry)) {
            char name[9];
            char extension[4];
            uint32_t size;
            uint16_t file_cluster;
            name[8] = ' ';
            extension[3] = ' ';
            memcpy(name, &(dirent->deName[0]), 8);
            memcpy(extension, dirent->deExtension, 3);

            if (name[0] == SLOT_EMPTY)
                return;

            /* skip over deleted entries */
            if (((uint8_t)name[0]) == SLOT_DELETED)
                continue;

            /* names are space padded - remove the spaces */
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
            if (strcmp(name, ".") == 0) {
                dirent++;
                continue;
            }
            if (strcmp(name, "..") == 0) {
                dirent++;
                continue;
            }

            if ((dirent->deAttributes & ATTR_VOLUME) != 0) {
            } else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
                file_cluster = getushort(dirent->deStartCluster);
                check_lost_files(usedClusters, file_cluster, image_buf, bpb);
            } else {
                /* We have a file. We should follow the file and remove all the used clusters from our collection! */
                size = getulong(dirent->deFileSize);
                uint16_t file_cluster_begin = getushort(dirent->deStartCluster);
                //uint16_t cluster, uint32_t bytes_remaining, uint8_t *image_buf, struct bpb33* bpb
                mark_file_clusters_used(usedClusters, file_cluster_begin, size, image_buf, bpb);
            }

            dirent++;
        }

        /* We've reached the end of the cluster for this directory. Where's the next cluster? */
        if (cluster == 0) {
            // root dir is special
            dirent++;
        } else {
            cluster = get_fat_entry(cluster, image_buf, bpb);
            dirent = (struct direntry*) cluster_to_addr(cluster, image_buf, bpb);
        }
    }
}

void free_clusters(uint16_t cluster_begin, uint16_t cluster_end, uint8_t *image_buf, struct bpb33* bpb) {
    uint16_t current_cluster = cluster_begin;

    while(1) {
        uint16_t next_cluster = get_fat_entry(current_cluster, image_buf, bpb);

        set_fat_entry(current_cluster, FAT12_MASK&CLUST_FREE, image_buf, bpb);

        if (current_cluster == cluster_end || is_end_of_file(next_cluster)) {
            break;
        }

        current_cluster = next_cluster;
    }

    set_fat_entry(cluster_begin, FAT12_MASK&CLUST_EOFS, image_buf, bpb);
}

void check_file_length(uint16_t cluster, uint8_t *image_buf, struct bpb33* bpb)
{
    struct direntry *dirent;
    int d, i;
    dirent = (struct direntry*) cluster_to_addr(cluster, image_buf, bpb);
    int clust_size = bpb->bpbBytesPerSec * bpb->bpbSecPerClust;

    while (1) {
        for (d = 0; d < clust_size; d += sizeof(struct direntry)) {
            char name[9];
            char extension[4];
            uint32_t size;
            uint16_t file_cluster;
            name[8] = ' ';
            extension[3] = ' ';
            memcpy(name, &(dirent->deName[0]), 8);
            memcpy(extension, dirent->deExtension, 3);

            if (name[0] == SLOT_EMPTY)
                return;

            /* skip over deleted entries */
            if (((uint8_t)name[0]) == SLOT_DELETED)
                continue;

            /* names are space padded - remove the spaces */
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
            if (strcmp(name, ".") == 0) {
                dirent++;
                continue;
            }
            if (strcmp(name, "..") == 0) {
                dirent++;
                continue;
            }

            if ((dirent->deAttributes & ATTR_VOLUME) != 0) {
                continue;
            } else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
                file_cluster = getushort(dirent->deStartCluster);
                check_file_length(file_cluster, image_buf, bpb);
            } else {
                size = getulong(dirent->deFileSize);
                file_cluster = getushort(dirent->deStartCluster);
                uint16_t fat_size_clusters = get_file_length(file_cluster, image_buf, bpb);

                uint32_t size_clusters = (size + (clust_size - 1)) / clust_size;
                uint32_t fat_size = fat_size_clusters * clust_size;
                if (size_clusters != fat_size_clusters) {
                    printf("%s.%s %u %u\n", name, extension, size, fat_size);

                    uint16_t begin_cluster = file_cluster + size_clusters - 1;
                    uint16_t end_cluster = file_cluster + fat_size_clusters;
                    free_clusters(begin_cluster, end_cluster, image_buf, bpb);
                }
            }

            dirent++;
        }

        /* We've reached the end of the cluster for this directory. Where's the next cluster? */
        if (cluster == 0) {
            // root dir is special
            dirent++;
        } else {
            cluster = get_fat_entry(cluster, image_buf, bpb);
            dirent = (struct direntry*) cluster_to_addr(cluster, image_buf, bpb);
        }
    }
}

void get_name(char *fullname, struct direntry *dirent)
{
    char name[9];
    char extension[4];
    int i;

    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);

    /* names are space padded - remove the padding */
    for (i = 8; i > 0; i--) {
        if (name[i] == ' ')
            name[i] = '\0';
        else
            break;
    }

    /* extensions aren't normally space padded - but remove the
       padding anyway if it's there */
    for (i = 3; i > 0; i--) {
        if (extension[i] == ' ')
            extension[i] = '\0';
        else
            break;
    }
    fullname[0]='\0';
    strcat(fullname, name);

    /* append the extension if it's not a directory */
    if ((dirent->deAttributes & ATTR_DIRECTORY) == 0) {
        strcat(fullname, ".");
        strcat(fullname, extension);
    }
}

/* write the values into a directory entry */
void write_dirent(struct direntry *dirent, char *filename,
           uint16_t start_cluster, uint32_t size)
{
    char *p, *p2;
    char *uppername;
    int len, i;

    /* clean out anything old that used to be here */
    memset(dirent, 0, sizeof(struct direntry));

    /* extract just the filename part */
    uppername = strdup(filename);
    p2 = uppername;
    for (i = 0; i < strlen(filename); i++) {
        if (p2[i] == '/' || p2[i] == '\\') {
            uppername = p2+i+1;
        }
    }

    /* convert filename to upper case */
    for (i = 0; i < strlen(uppername); i++) {
        uppername[i] = toupper(uppername[i]);
    }

    /* set the file name and extension */
    memset(dirent->deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (p == NULL) {
        fprintf(stderr, "No filename extension given - defaulting to .___\n");
    } else {
        *p = '\0';
        p++;
        len = strlen(p);
        if (len > 3) len = 3;
            memcpy(dirent->deExtension, p, len);
    }
    if (strlen(uppername)>8) {
        uppername[8]='\0';
    }
    memcpy(dirent->deName, uppername, strlen(uppername));
    free(p2);

    /* set the attributes and file size */
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, start_cluster);
    putulong(dirent->deFileSize, size);

    /* a real filesystem would set the time and date here, but it's
       not necessary for this coursework */
}

/* create_dirent finds a free slot in the directory, and write the
   directory entry */
void create_dirent(struct direntry *dirent, char *filename,
           uint16_t start_cluster, uint32_t size,
           uint8_t *image_buf, struct bpb33* bpb)
{
    while(1) {
        if (dirent->deName[0] == SLOT_EMPTY) {
            /* we found an empty slot at the end of the directory */
            write_dirent(dirent, filename, start_cluster, size);
            dirent++;

            /* make sure the next dirent is set to be empty, just in
               case it wasn't before */
            memset((uint8_t*)dirent, 0, sizeof(struct direntry));
            dirent->deName[0] = SLOT_EMPTY;
            return;
        }
        if (dirent->deName[0] == SLOT_DELETED) {
            /* we found a deleted entry - we can just overwrite it */
            write_dirent(dirent, filename, start_cluster, size);
            return;
        }
        dirent++;
    }
}

void usage()
{
    fprintf(stderr, "Usage: dos_scandisk <imagename>\n");
    exit(1);
}

int main(int argc, char** argv)
{
    uint8_t *image_buf;
    int fd;
    struct bpb33* bpb;
    if (argc != 2) {
        usage();
    }

    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);

    int total_clusters = bpb->bpbSectors / bpb->bpbSecPerClust;
    int clust_size = bpb->bpbSecPerClust * bpb->bpbBytesPerSec;
    int used_clusters[total_clusters];
    check_lost_files(used_clusters, 0, image_buf, bpb);

    int i;
    int shownPrefix = 0;
    for (i = 2; i < total_clusters; i++) {
        if (used_clusters[i] == 0 && get_fat_entry(i, image_buf, bpb) != CLUST_FREE) {
            if (!shownPrefix) {
                printf("Unreferenced:");
                shownPrefix = 1;
            }
            printf(" %i", i);
        }

        if (i == total_clusters - 1 && shownPrefix) {
            printf("\n");
        }
    }

    int foundCount = 1;
    shownPrefix = 0;
    for (i = 2; i < total_clusters; i++) {
        if (used_clusters[i] == 0 && get_fat_entry(i, image_buf, bpb) != CLUST_FREE) {
            if (!shownPrefix) {
                printf("Lost File: ");
            }

            uint16_t size = get_file_length(i, image_buf, bpb);
            printf("%i %i\n", i, size);

            struct direntry *dirent = (struct direntry*) cluster_to_addr(0, image_buf, bpb);
            uint32_t size_bytes = size * clust_size;

            const char base[] = "found";
            const char extension[] = ".dat";
            char filename [13];
            sprintf(filename, "%s%i%s", base, foundCount++, extension);

            create_dirent(dirent, filename, i, size_bytes, image_buf, bpb);

            check_lost_files(used_clusters, 0, image_buf, bpb);
        }

        if (i == total_clusters - 1 && shownPrefix) {
            printf("\n");
        }
    }

    check_file_length(0, image_buf, bpb);

    close(fd);
    exit(0);
}