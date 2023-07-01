#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>


#define	TAR_BLOCK_SIZE		512


#define UNUSED(x) (void)(x)

typedef unsigned int uint;
typedef unsigned long ulong;

typedef struct {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8];
    char typeflag;
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajr[8];
	char devminor[8];
	char prefix[155];
	char padding[TAR_BLOCK_SIZE - 500];
} tar_header_t;

typedef struct {
	char bytes[TAR_BLOCK_SIZE];
} tar_buffer;


typedef struct {
	char operation;
	char **files;
	char *archive_file;
	bool voption;
	int file_count;
} user_arguments;



void *get_memory(size_t bytes) {
	void *ptr = malloc(bytes);
	if (!ptr)
		errx(1, "Out of memory");
	return ptr;
}


void process_option(char *arg, char **argv, int *i, user_arguments *args) {
    switch (arg[1]) {
        case 'x':
        case 't':
            if (args->operation)
                errx(2, "choose -t or -x option");
            args->operation = arg[1];
            break;
        case 'f':
            args->archive_file = argv[++(*i)];
            break;
        case 'v':
            args->voption = true;
            break;
        default:
            errx(2, "No such option \"%s\"", arg);
    }
}

void process_file(char *arg, user_arguments *args) {
    args->files[args->file_count] = arg;
    args->file_count++;
}


void get_args(int argc, char **argv, user_arguments *args) {
    memset(args, 0, sizeof(user_arguments));
    args->files = (char **)(get_memory(argc * sizeof(char *)));

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        
        if (arg[0] == '-') {
            process_option(arg, argv, &i, args);
        } else {
            process_file(arg, args);
        }
    }

}


/*
	
*/

long get_block_number(size_t offset) {
    return offset / TAR_BLOCK_SIZE + !!(offset % TAR_BLOCK_SIZE);
}

size_t get_entry_size(tar_header_t* header) {
    int reported_size = strtol(header->size, NULL, 8);
    size_t block_number = get_block_number(reported_size);
    return block_number * TAR_BLOCK_SIZE;
}


size_t get_filesize(FILE* f) {
    fseek(f, 0L, SEEK_END);
    size_t sz = (size_t)ftell(f);
    if (sz == (size_t)-1) {
        errx(2, "error in ftell");
    }
    fseek(f, 0L, SEEK_SET);
    return sz;
}

void magic(tar_header_t *header) {
	if (!!memcmp(header->magic, "ustar", sizeof("ustar")) && !!memcmp(header->magic, "ustar  ", sizeof("ustar  "))) {
		warnx("This does not look like a tar archive");
		errx(2, "Exiting with failure status due to previous errors");
	}
}


bool file_in_args(const char *filename, user_arguments* args) {
    for (int i = 0; i < args->file_count; ++i) {
        if (strcmp(filename, args->files[i]) == 0) {
            args->files[i][0] = '\0';
            return true;
        }
    }
    return false;
}


void list_tar_entry(tar_header_t *header, user_arguments *args, FILE *archive) {
    printf("%s\n", header->name);
    UNUSED(archive);
    UNUSED(args);

}


void extract_entry(tar_header_t *header, user_arguments *args, FILE *archive) {
	FILE *f = fopen(header->name, "w");
	
	/**/
	long cur_pos = ftell(archive);

	//int f_size = octal_to_int(header->size);
	int f_size = strtol(header->size, NULL, 8);
	tar_buffer block;
	for (int i = 0; i < f_size / TAR_BLOCK_SIZE; ++i) {
		size_t bytes_read = fread(&block, 1, TAR_BLOCK_SIZE, archive);
		fwrite(&block, bytes_read, 1, f);
	}

	fseek(archive, cur_pos, SEEK_SET);
	fclose(f);

	if (args->voption)
		printf("%s\n", header->name);
}


void* get_ptr(user_arguments *args) {
    switch (args->operation) {
        case 'x':
            return extract_entry;
        case 't':
            return list_tar_entry;
    }
    errx(2,"bad options");
}


void validate_footer(FILE* archive) {
    const size_t footer_size = sizeof(char) * 2 * TAR_BLOCK_SIZE;
    const char null_block[TAR_BLOCK_SIZE] = {0};

    long cur_pos = ftell(archive);
    fseek(archive, -footer_size, SEEK_END);

    char* footer_buffer = (char*)get_memory(footer_size);
    fread(footer_buffer, sizeof(char), footer_size, archive);

    if (!memcmp(null_block, footer_buffer + TAR_BLOCK_SIZE, TAR_BLOCK_SIZE) &&
        memcmp(null_block, footer_buffer, TAR_BLOCK_SIZE)) {
        warnx("A lone zero block at %lu", get_block_number(ftell(archive)));
    }

    fseek(archive, cur_pos, SEEK_SET);
    free(footer_buffer);
}

bool reached_EOF(const tar_header_t* header, FILE* archive) {
    const char null_block[TAR_BLOCK_SIZE] = {0};
    return (memcmp(null_block, header, TAR_BLOCK_SIZE) == 0) || feof(archive);
}


void check_args(user_arguments* args) {
    bool found_error = false;

    for (int i = 0; i < args->file_count; ++i) {
        if ((args->files)[i][0] != '\0') {
            warnx("%s: Not found in archive", args->files[i]);
            found_error = true;
        }
    }

    if (found_error) {
        errx(2, "Exiting with failure status due to previous errors");
    }
}


void execute_action_tar(user_arguments* args, FILE* archive) {
    size_t file_size = get_filesize(archive);
    typedef void (*op)(tar_header_t*, user_arguments*, FILE*);
    op operation = get_ptr(args);
    tar_header_t header;
    int entry_size;

    do {
        if (fread(&header, TAR_BLOCK_SIZE, 1, archive) != 1) {
            break;
        }

        if (reached_EOF(&header, archive)) {
            break;
        }

        if (file_in_args(header.name, args) || args->file_count == 0) {
	    magic(&header);
            char flag = header.typeflag;
            if (!(flag == '0' || flag == '\0')) {
                errx(2, "Unsupported header type: %d", (int)flag);
            }
            operation(&header, args, archive);
            fflush(stdout);
        }

        entry_size = get_entry_size(&header);
        fseek(archive, entry_size, SEEK_CUR);
    } while (true);

    
    size_t cur_pos = ftell(archive) - entry_size;
    if (cur_pos + entry_size > file_size) {
        warnx("Unexpected EOF in archive");
        errx(2, "Error is not recoverable: exiting now");
    }
    
}


int main(int argc, char **argv) {
	user_arguments args;
	get_args(argc, argv, &args);

	FILE *archive = fopen(args.archive_file, "r");
	
	if (archive == NULL) {
		errx(2, "Error, exiting");
	}
	
	execute_action_tar(&args, archive);
	validate_footer(archive);
	check_args(&args);

	fclose(archive);

	return 0;
}

