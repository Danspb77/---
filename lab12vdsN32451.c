#define _XOPEN_SOURCE 500   // for nftw()

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ftw.h>
#include <sys/param.h>      // for MIN()
#include <getopt.h>
#include <dlfcn.h>

#include "plugin_api.h"

#define MAX_INDENT_LEVEL 128

void optparse(int argc, char *argv[]);

void walk_dir(const char *dir);

unsigned char *search_bytes;
typedef int (*ppf_func_t)(const char*, struct option*, size_t);
typedef int (*pgi_func_t)(struct plugin_info*);
int search_bytes_size;
typedef struct{
    void* lib;
    struct plugin_info pi;
    ppf_func_t ppf;
    struct option* in_opts;
    size_t in_opts_len;
} dynamic_lib; //структура в которой будем хранить всю информацию о плагине
//чтобы не было много глобальных переменных, а все в одной структуре
//+ ее легко тогда освободить чтобы заново открыть плагины при -P
dynamic_lib *plugins = NULL;
int plug_cnt = 0;
int or = 0, not = 0, found_opts = 0, got_opts = 0;

void open_dyn_libs(const char *dir);

int main(int argc, char *argv[]) {
    open_dyn_libs("./"); //открываем библиотеки в текущей папке
    optparse(argc, argv); //и парсим все опции
    if (got_opts == 0) {
        printf("No options found. Use -h for help\n");
        if(plugins){
        for(int i = 0; i < plug_cnt; i++) {
            if(plugins[i].in_opts) free(plugins[i].in_opts);
            dlclose(plugins[i].lib);
        }
        free(plugins);
        }   
        exit(EXIT_FAILURE);
    }

    walk_dir(argv[argc-1]);

    if(plugins){
        for(int i = 0; i < plug_cnt; i++) {
            if(plugins[i].in_opts) free(plugins[i].in_opts);
            dlclose(plugins[i].lib);
        }
        free(plugins);
    }   
    return EXIT_SUCCESS;
}


int open_func(const char *fpath,const struct stat *sb, 
        int typeflag, struct FTW *ftwbuf) {
    //также как и walk_func, но только здесь мы открываем плагины
    if(!ftwbuf || !sb) return 1;
    if(typeflag == FTW_F && strstr(fpath, ".so") != NULL){
        void *library = dlopen(fpath, RTLD_LAZY);
        if(!library) {
            fprintf(stderr, "dlopen() failed: %s\n", dlerror());
            return 0; //если с плагином проблемы - ищем следующие
        } else {
            void* pi_f = dlsym(library, "plugin_get_info");
            if(!pi_f) {
            fprintf(stderr, "dlsym() failed: %s\n", dlerror());
            dlclose(library);
            return 0;
            } 
            void* pf_f = dlsym(library, "plugin_process_file");
            if(!pf_f) {
            fprintf(stderr, "dlsym() failed: %s\n", dlerror());
            dlclose(library);
            return 0;
            }
            struct plugin_info pi ={0};
            pgi_func_t pgi = (pgi_func_t)pi_f;
            int tmp = pgi(&pi);
            if(tmp == -1){
                fprintf(stderr, "error in plugin_get_info\n");
                dlclose(library);
                return 0;
            }

            //если с плагином все в порядке сохраняем это все чтобы потом обработать и использовать
            plugins = realloc(plugins, sizeof(dynamic_lib) * (plug_cnt+1));
            plugins[plug_cnt].pi = pi;
            plugins[plug_cnt].ppf = (ppf_func_t)pf_f;
            plugins[plug_cnt].lib = library;
            plugins[plug_cnt].in_opts = NULL;
            plugins[plug_cnt].in_opts_len = 0;
            plug_cnt++;
            found_opts+=pi.sup_opts_len;
        }

    }
    return 0;
}

void open_dyn_libs(const char *dir){
    int res = nftw(dir, open_func, 10, FTW_PHYS); //для открытия плагинов
    if (res < 0) {
        fprintf(stderr, "ntfw() failed: %s\n", strerror(errno));
    }
}

void optparse(int argc, char *argv[]){
    struct option *long_options = calloc(found_opts+1, sizeof(struct option));
    int copied = 0;
    for(int i = 0; i < plug_cnt; i++) {
        for(size_t j = 0; j < plugins[i].pi.sup_opts_len; j++){
            long_options[copied] = plugins[i].pi.sup_opts[j].opt;
            copied++;
        }
    }
    int option_index = 0;
    int choice;
    while ((choice = getopt_long(argc, argv, "vhP:OAN", long_options, &option_index)) != -1)
    {
        switch (choice)
        {
        case 0:
            for(int i = 0; i < plug_cnt; i++) {
                    for(size_t j = 0; j < plugins[i].pi.sup_opts_len; j++){
                        if(strcmp(long_options[option_index].name, plugins[i].pi.sup_opts[j].opt.name) == 0){
                            plugins[i].in_opts =  realloc(plugins[i].in_opts, (plugins[i].in_opts_len+1) * sizeof(struct option));
                            plugins[i].in_opts[plugins[i].in_opts_len] = long_options[option_index];
                            if(plugins[i].in_opts[plugins[i].in_opts_len].has_arg != 0)plugins[i].in_opts[plugins[i].in_opts_len].flag = (int*)optarg;
                            plugins[i].in_opts_len++;
                            got_opts++;
                        }
                    }
            }
            break;
        case 'h':
            printf("Usage: %s <options> <dir>\n", argv[0]);
            printf("<dir> - directory to search\n");
            printf("available options: -h for help  -P <dir> to change plugin dir -O for 'or'  -A for 'and'  -N for 'not'\n");
            for(int i = 0; i < plug_cnt; i++){
                printf("Plugin purpose: %s\n", plugins[i].pi.plugin_purpose);
                for(size_t j = 0; j < plugins[i].pi.sup_opts_len; j++) printf("%s -- %s\n", plugins[i].pi.sup_opts[j].opt.name, plugins[i].pi.sup_opts[j].opt_descr);
                printf("\n");
            }
            if(plugins){
                for(int i = 0; i < plug_cnt; i++) {
                    if(plugins[i].in_opts) free(plugins[i].in_opts);
                    dlclose(plugins[i].lib);
                }
                free(plugins);
            }   
            free(long_options);
            exit(EXIT_SUCCESS);
        case 'v':
            printf("Shurygin Danil N3245 Version 1.0\n");
            if(plugins){
                for(int i = 0; i < plug_cnt; i++) {
                    if(plugins[i].in_opts) free(plugins[i].in_opts);
                    dlclose(plugins[i].lib);
                }
                free(plugins);
            }   
            free(long_options);
            exit(EXIT_SUCCESS);
        case 'P':
            if(got_opts > 0){
                fprintf(stderr, "-P must be before plugin opts!\n");
                free(long_options); //если мы уже нашли какую то опцию до -P то это может вызвать проблемы.
                got_opts = 0;
                return;
            }
            for(int i = 0; i < plug_cnt; i++) {
                dlclose(plugins[i].lib);
            }
            free(plugins);
            plugins = NULL;
            plug_cnt = 0;
            found_opts = 0;
            
            if(getenv("LAB1DEBUG") != NULL) fprintf(stderr, "New lib path: %s\n", optarg);
            free(long_options);
            open_dyn_libs(optarg);
            long_options = calloc(found_opts+1, sizeof(struct option));
            copied = 0;
            for(int i = 0; i < plug_cnt; i++) {
            for(size_t j = 0; j < plugins[i].pi.sup_opts_len; j++){
                long_options[copied] = plugins[i].pi.sup_opts[j].opt;
                copied++;
                }
            }
            break;
        case 'O':
            or = 1;
            break;
        case 'A':
            or = 0;
            break;
        case 'N':
            not = 1;
            break;
        case '?':
            break;
        }
    }
    free(long_options);
}

void print_entry(int level, int type, const char *path) {
    if (!strcmp(path, ".") || !strcmp(path, "..") || type!=FTW_F)
        return; //открываем только обычные файлы

    char indent[MAX_INDENT_LEVEL] = {0};
    memset(indent, ' ', MIN((size_t)level, MAX_INDENT_LEVEL));

    int cnt = 0;
    int cnt_success = 0;
    for(int i = 0; i < plug_cnt; i++){
        //если не нашли опций у плагина то не запускаем из него функцию
        if(plugins[i].in_opts_len > 0){
            int tmp = plugins[i].ppf(path, plugins[i].in_opts, plugins[i].in_opts_len);
            if(tmp == -1){
                fprintf(stderr, "error in plugin! %s", strerror(errno));
                if(errno == EINVAL || errno == ERANGE) plugins[i].in_opts_len = 0;
            } else if (tmp == 0) cnt++;
            cnt_success++;
        }
    }
    
    if((not && or && (cnt== 0)) || (not && !or && (cnt!=cnt_success))){
        printf("%sFound file: %s\n", indent, path);
    } else if((!not && or && (cnt > 0)) || (!not && !or && (cnt==cnt_success))){
        printf("%sFound file: %s\n", indent, path);
    }
    return;
} 

int walk_func(const char *fpath,const struct stat *sb, 
        int typeflag, struct FTW *ftwbuf) {
    if(!sb) return -1;
    print_entry(ftwbuf->level, typeflag, fpath); 
   
    return 0;
}

void walk_dir(const char *dir) {
    int res = nftw(dir, walk_func, 10, FTW_PHYS);   
    if (res < 0) {
        fprintf(stderr, "ntfw() failed: %s\n", strerror(errno));
    }
}
