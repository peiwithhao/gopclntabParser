#define _GNU_SOURCE
// #include <bits/getopt_ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <elf.h>
#include <getopt.h>

size_t gopclntab_offset;
size_t gopclntab_size;

struct runtime_pcHeader {
  uint32_t magic;            // See "Version" section.
  uint8_t pad[2];            // Zero.
  uint8_t minLC;             // Aka "quantum". Pointer alignment.
  uint8_t ptrSize;           // Pointer size: 4 or 8
  int64_t nfunc;             // Number of entries in the function table.
  unsigned nfiles;           // Number of entries in the file table.
  uintptr_t textStart;       // Base address for text section references.
  uintptr_t funcnameOffset;  // Offset to function name region.
  uintptr_t cuOffset;        // Offset to compilation unit table region.
  uintptr_t filetabOffset;   // Offset to file name region.
  uintptr_t pctabOffset;     // Offset to PC data region.
  uintptr_t pclnOffset;      // Offset to function data region.
};

struct runtime_functab {
  uint32_t entryoff; // First byte of function code relative to textStart.
  uint32_t funcoff;  // Offset relative to the function data region start.
};

struct runtime__func {
  uint32_t entryOff;    // First byte of function code relative to textStart.
  int32_t nameOff;      // Function name (runtime.funcnametab offset).
  int32_t args;         // Number of arguments.
  uint32_t deferreturn; // Information about `defer` statements (?).
  uint32_t pcsp;        // PC<->stack delta mappings (runtime.pcdata offset)
  uint32_t pcfile;      // PC<->CU file index mappings (runtime.pcdata offset)
  uint32_t pcln;        // PC<->Line number mappings (runtime.pcdata offset)
  uint32_t npcdata;     // Number of dynamic PC data offsets.
  uint32_t cuOffset;    // Base index of the CU (runtime.cutab index)
  int32_t startLine;    // Line number of the first declaration character (Go v1.20+)
  uint8_t funcID;       // Function ID (only set for certain RT funcs, otherwise 0)
  uint8_t flag;         // Unknown flags.
  uint8_t _[1];         // Padding.
  uint8_t nfuncdata;    // Number of dynamic `go:func.*` offsets.

  // // Pseudo-fields (data following immediately after)
  // uint32_t pcdata[npcdata]; // `runtime.pcdata` offsets.
  // uint32_t funcdata[nfuncdata]; // `go:func.*` offsets (Go >= v1.18).
};


int main(int argc, char ** argv){
    int opt;
    char *filename = NULL;
    char *search_funcname = NULL;
    int verbose = 0;

    static struct option long_options[] = {
        {"File", required_argument, 0, 'f'},
        {"Verbose", no_argument, 0, 'v'},
        {"funcname", required_argument, 0, 'n'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "f:vn:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'f':
                filename = optarg;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'n':
                search_funcname = optarg;
                break;
            case 'h':
            default:
                printf("Usage: %s [OPTIONS]\n", argv[0]);
                printf("Options:\n");
                printf("\t-f, --file FILENAME\tSpecify the input file\n");
                printf("\t-v, --verbose      \tEnable verbose output\n");
                printf("\t-n, --funcname     \tSpecify the searched funcname\n");
                printf("\t-h, --help         \tDisplay this help message\n");
                exit(opt == 'h' ? 0:1);
        }
    }

    if(!filename || !search_funcname) {
        fprintf(stderr, "Error: -f and -n are required\n");
        exit(1);
    }

    int read_bytes = 0;
    int fd = open(filename, O_RDONLY);
    if(fd < 0){
        perror("file open");
        exit(1);
    }
    // read elf header
    Elf64_Ehdr *elf_head = malloc(sizeof(Elf64_Ehdr));
    read_bytes = read(fd, elf_head, sizeof(Elf64_Ehdr));
    if(read_bytes < 0){
        perror("read");
        exit(1);
    }
    if(elf_head->e_ident[0]!=0x7f || elf_head->e_ident[1]!='E' || elf_head->e_ident[2] != 'L' || elf_head->e_ident[3] != 'F') {
        printf("[X]file format is not ELF...\n");
        exit(0);
    }
    // analysis section
    // get section header table
    Elf64_Shdr *shdr = malloc(sizeof(Elf64_Shdr)*elf_head->e_shnum);
    lseek(fd, elf_head->e_shoff, SEEK_SET);
    read_bytes = read(fd, shdr, sizeof(Elf64_Shdr)*elf_head->e_shnum);
    if(read_bytes < 0){
        perror("read");
        exit(1);
    }
    // get section header str 
    lseek(fd, shdr[elf_head->e_shstrndx].sh_offset, SEEK_SET);
    char shstrtab[shdr[elf_head->e_shstrndx].sh_size];
    char *temp = shstrtab;
    read_bytes = read(fd, shstrtab, shdr[elf_head->e_shstrndx].sh_size);

    if(read_bytes < 0){
        perror("read");
        exit(1);
    }
    for (int i = 0; i < elf_head->e_shnum; i++){
        temp = shstrtab;
        temp += shdr[i].sh_name;
        if (strcmp(temp, ".gopclntab") == 0) {
            gopclntab_offset = shdr[i].sh_offset;
            gopclntab_size  = shdr[i].sh_size;
            break;
        }
    }

    if (gopclntab_offset == 0) {
        printf("[x]No found .gopclntab\n");
        exit(0);
    }
    if (verbose){
        printf("offset: %p, size: %p\n", gopclntab_offset, gopclntab_size);
    }
    struct runtime_pcHeader *data = malloc(gopclntab_size);
    lseek(fd, gopclntab_offset, SEEK_SET);
    read_bytes = read(fd, data, gopclntab_size);
    if(read_bytes < 0){
        perror("read");
        exit(1);
    }
    if(verbose){
        printf("[+]============gopclntab_parser ===========\n");
        printf("magic: 0x%x\n", data->magic);
        printf("nfunc: %p\n", data->nfunc);
        printf("textStart: %p\n", data->textStart);
        printf("func_name_offset: %p\n", data->funcnameOffset);
    }

    char *funcnametable = (char *)((char *)data + data->funcnameOffset);
    char *cutab = (char *)((char *)data + data->cuOffset);
    char *pclntab =(char *)((char *)data + data->pclnOffset); 
    if(verbose){
        printf("funcNameTable: %p\n", funcnametable);
        printf("cutable: %p\n", cutab);
        printf("pclntab : %p\n", pclntab);
        printf("funcnametable: %s\n", funcnametable);
    }

    for(int i = 0; i < data->nfunc; i++) {
        struct runtime_functab *tmp = (struct runtime_functab *)(pclntab + i * sizeof(struct runtime_functab));
        // printf("runtime_func entry %p\n", pclntab + tmp->funcoff);
        // printf("entryoff %p\n", tmp->entryoff);
        struct runtime__func * temp_func = (struct runtime__func *)(pclntab+tmp->funcoff);
        const char *funcName = (const char *)(funcnametable + temp_func->nameOff);
        if (verbose) {
            printf("funcName: %s, \n\tentryoff: %p\n", funcName, tmp->entryoff + data->textStart); 
        }
        if(!strcmp(funcName, search_funcname)){
            printf("address: %p\n", tmp->entryoff + data->textStart); 
        }
    }

    // clean
    free(data);
    free(shdr);
    free(elf_head);
    close(fd);
}


