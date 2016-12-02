/* 3005 coursework 2 */

void get_name(char *fullname, struct direntry *dirent);

struct direntry* find_file(char *infilename, uint16_t cluster, int find_mode, uint8_t *image_buf, struct bpb33* bpb);

void copy_out_file(FILE *fd, uint16_t cluster, uint32_t bytes_remaining, uint8_t *image_buf, struct bpb33* bpb);

void copyout(char *infilename, char* outfilename, uint8_t *image_buf, struct bpb33* bpb);

uint16_t copy_in_file(FILE* fd, uint8_t *image_buf, struct bpb33* bpb,  uint32_t *size);

void write_dirent(struct direntry *dirent, char *filename, uint16_t start_cluster, uint32_t size);

void create_dirent(struct direntry *dirent, char *filename, uint16_t start_cluster, uint32_t size, uint8_t *image_buf, struct bpb33* bpb);

void copyin(char *infilename, char* outfilename, uint8_t *image_buf, struct bpb33* bpb);

void usage();