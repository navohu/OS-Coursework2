#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"

typedef struct tuple{
    int startCluster;
    int blockLength;
} tTuple;

typedef struct node {
    struct tuple val;
    struct node * next;
} node_t;

void push(node_t * head, struct tuple val) {
    node_t * current = head;
    while (current->next != NULL) {
        current = current->next;
    }

    /* now we can add a new variable */
    current->next = malloc(sizeof(node_t));
    current->next->val = val;
    current->next->next = NULL;
}

void mark_references(uint16_t cluster, int *referenced, uint32_t bytes_remaining, uint8_t *image_buf, struct bpb33* bpb){
    int clust_size = bpb->bpbSecPerClust * bpb->bpbBytesPerSec;
    int total_clusters = bpb->bpbSectors / bpb->bpbSecPerClust;

    referenced[cluster] = 1;
    
    if (cluster == 0) {
        fprintf(stderr, "Bad file termination\n");
        return;
    } else if (cluster > total_clusters) {
        abort(); /* this shouldn't happen */
    }

    uint16_t next_cluster = get_fat_entry(cluster, image_buf, bpb);

    if (is_end_of_file(next_cluster)) {
        return;
    } else {
        mark_references(get_fat_entry(cluster, image_buf, bpb), referenced, bytes_remaining - clust_size, image_buf, bpb);
    }
}

void check_references(int *referenced, uint8_t *image_buf, struct bpb33* bpb){
    int total_clusters = bpb->bpbSectors / bpb->bpbSecPerClust;
    int shownPrefix = 0;
    for (int i = 2; i <= total_clusters; ++i)
    {
        if (referenced[i] == 0 && get_fat_entry(i, image_buf, bpb) != CLUST_FREE){
            if(!shownPrefix){
                printf("Unreferenced: ");
                shownPrefix = 1;
            }
            printf(" %i", i);
        }
        if (i == total_clusters - 1 && shownPrefix) {
            printf("\n");
        }
    }
}

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

/* write the values into a directory entry -- from dos_cp.c*/
void write_dirent(struct direntry *dirent, char *filename, uint16_t start_cluster, uint32_t size)
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

void create_direntry(int i, uint16_t size, int foundCount, uint8_t *image_buf, struct bpb33* bpb){
    struct direntry *dirent = (struct direntry*) cluster_to_addr(0, image_buf, bpb);
    int clust_size = bpb->bpbSecPerClust * bpb->bpbBytesPerSec;
    uint32_t size_bytes = size * clust_size;
    const char base[] = "found";
    const char extension[] = ".dat";
    char filename [13];
    printf("Created file: %s%i%s\n", base, foundCount, extension);
    sprintf(filename, "%s%i%s", base, foundCount, extension);

    while(1){
        if(dirent->deName[0] == SLOT_EMPTY){ //found empty slot
            write_dirent(dirent, filename, i, size_bytes);
            dirent++;

            memset((uint8_t*)dirent, 0, sizeof(struct direntry));
            dirent->deName[0] = SLOT_EMPTY;
            return;
        }
        if(dirent->deName[0] == SLOT_DELETED){
            write_dirent(dirent, filename, i, size_bytes);
            return;
        }
        dirent++;
    }
}

void print_list(node_t * head) {
    node_t * current = head;

    while (current != NULL) {
        printf("%i %i\n", current->val.startCluster, current->val.blockLength);
        current = current->next;
    }
}

void lost_files(int *referenced, uint8_t *image_buf, struct bpb33* bpb){
    int total_clusters = bpb->bpbSectors / bpb->bpbSecPerClust;
    int shownPrefix = 0;
    int foundCount = 0;
    for (int i = 2; i <= total_clusters; ++i)
    {
        if (referenced[i] == 0 && get_fat_entry(i, image_buf, bpb) != CLUST_FREE){
            if(!shownPrefix){
                uint16_t size = get_file_length(i, image_buf, bpb);
                printf("Lost File: %i %i\n", i, size);
                create_direntry(i, size, foundCount, image_buf, bpb);
                shownPrefix = 1;
                foundCount++;
            }
            else if(i + 1 != get_fat_entry(i, image_buf, bpb)){
                shownPrefix = 0;
            }
        }
    }

}

void follow_dir(uint16_t cluster, int indent, uint8_t *image_buf, struct bpb33* bpb, int *referenced)
{
    referenced[cluster] = 1;
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
                //Nothing
            }
            else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {  //If current dirent is a directory
                file_cluster = getushort(dirent->deStartCluster);
                follow_dir(file_cluster, indent+2, image_buf, bpb, referenced);
            } else {                                             //If it is a file
                size = getulong(dirent->deFileSize);

                /* MARKS THE REFERENCES, QUESTION 1*/
                mark_references(getushort(dirent->deStartCluster), referenced, size, image_buf, bpb); 
            }
            dirent++; //Go to next directory entry
        }
    }
}


void usage()
{
    fprintf(stderr, "Usage: ./dos_scandisk <imagename>\n");
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

    int *referenced = calloc(sizeof(int), 4096);
    follow_dir(0,0,image_buf,bpb, referenced);
    check_references(referenced, image_buf, bpb);
    lost_files(referenced, image_buf, bpb);

    close(fd);
    exit(0);
}
