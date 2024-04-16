#define _XOPEN_SOURCE 500   // для nftw()

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ftw.h>
#include <sys/param.h>      // для MIN()
#include <getopt.h>
#include <dlfcn.h>

#include "plugin_api.h"

#define MAX_INDENT_LEVEL 128

// Function declarations
int open_func(const char *fpath,const struct stat *sb, 
        int typeflag, struct FTW *ftwbuf);
void open_dyn_libs(const char *dir);
void optparse(int argc, char *argv[]);
void walk_dir(const char *dir);

void print_entry(int level, int type, const char *path);
int walk_func(const char *fpath,const struct stat *sb, 
        int typeflag, struct FTW *ftwbuf);

// Function pointers
unsigned char *search_bytes;
typedef int (*ppf_func_t)(const char*, struct option*, size_t);
typedef int (*pgi_func_t)(struct plugin_info*);


typedef struct{
    void* lib;
    struct plugin_info pi;
    ppf_func_t ppf;
    struct option* in_opts;
    size_t in_opts_len;
} dynamic_lib; 

dynamic_lib *plugins = NULL;
int plug_cnt = 0;
int or = 0, not = 0, found_opts = 0, got_opts = 0;

// Implementation of open_func
int open_func(const char *fpath, const struct stat *sb, 
              int typeflag, struct FTW *ftwbuf) {
    // Проверяем, что все параметры используются, чтобы избежать предупреждения компилятора
    (void) sb;
    (void) typeflag;
    (void) ftwbuf;

    if (!fpath) {
        fprintf(stderr, "Invalid file path\n");
        return 0;
    }

    if (typeflag == FTW_F && strstr(fpath, ".so") != NULL) {
        // Открываем динамическую библиотеку
        void *library = dlopen(fpath, RTLD_LAZY);
        if (!library) {
            // В случае ошибки выводим сообщение об ошибке и возвращаем 0,
            // чтобы пропустить текущую библиотеку и продолжить поиск
            fprintf(stderr, "dlopen() failed for %s: %s\n", fpath, dlerror());
            return 0;
        } else {
            // Получаем указатели на функции плагина
            void* pi_f = dlsym(library, "plugin_get_info");
            if (!pi_f) {
                // Если не удалось найти функцию plugin_get_info, выводим ошибку
                fprintf(stderr, "dlsym() failed for plugin_get_info: %s\n", dlerror());
                dlclose(library); // Закрываем библиотеку
                return 0;
            } 
            
            void* pf_f = dlsym(library, "plugin_process_file");
            if (!pf_f) {
                // Если не удалось найти функцию plugin_process_file, выводим ошибку
                fprintf(stderr, "dlsym() failed for plugin_process_file: %s\n", dlerror());
                dlclose(library); // Закрываем библиотеку
                return 0;
            }

            // Вызываем функцию plugin_get_info для получения информации о плагине
            struct plugin_info pi = {0};
            pgi_func_t pgi = (pgi_func_t)pi_f;
            int tmp = pgi(&pi);
            if (tmp == -1) {
                // Если возникла ошибка при получении информации о плагине, выводим ошибку
                fprintf(stderr, "Error in plugin_get_info\n");
                dlclose(library); // Закрываем библиотеку
                return 0;
            }

            // Если все успешно, сохраняем информацию о плагине в массив
            plugins = realloc(plugins, sizeof(dynamic_lib) * (plug_cnt + 1));
            plugins[plug_cnt].pi = pi;
            plugins[plug_cnt].ppf = (ppf_func_t)pf_f;
            plugins[plug_cnt].lib = library;
            plugins[plug_cnt].in_opts = NULL;
            plugins[plug_cnt].in_opts_len = 0;
            plug_cnt++;
            found_opts += pi.sup_opts_len;
        }
    }
    
    return 0; // Возвращаем 0, чтобы продолжить обход директории
}





int main(int argc, char *argv[]) {
    open_dyn_libs("./"); // Открытие динамических библиотек в текущей папке
    optparse(argc, argv); // Парсинг опций командной строки

    // Проверка, были ли найдены опции. Если нет, выводим сообщение и завершаем программу
    if (got_opts == 0) {
        printf("No options found. Use -h for help\n");

        // Освобождаем выделенную память и закрываем открытые библиотеки
        if (plugins) {
            for (int i = 0; i < plug_cnt; i++) {
                if (plugins[i].in_opts) free(plugins[i].in_opts);
                dlclose(plugins[i].lib);
            }
            free(plugins);
        }
        exit(EXIT_FAILURE); // Выходим с кодом ошибки
    }

    // Обход директории, указанной в последнем аргументе командной строки
    walk_dir(argv[argc-1]);

    // Освобождение памяти и закрытие открытых библиотек
    if (plugins) {
        for (int i = 0; i < plug_cnt; i++) {
            if (plugins[i].in_opts) free(plugins[i].in_opts);
            dlclose(plugins[i].lib);
        }
        free(plugins);
    }   

    return EXIT_SUCCESS; // Возвращаем успешный код завершения программы
}

// Функция открытия динамических библиотек
void open_dyn_libs(const char *dir){
    int res = nftw(dir, open_func, 10, FTW_PHYS); // Для открытия плагинов
    if (res < 0) {
        fprintf(stderr, "ntfw() failed: %s\n", strerror(errno));
    }
}

// Функция парсинга опций командной строки
void optparse(int argc, char *argv[]){
    // Выделяем память под структуры опций
    struct option *long_options = calloc(found_opts+1, sizeof(struct option));
    int copied = 0;
    // Копируем опции из всех плагинов в общий список опций
    for(int i = 0; i < plug_cnt; i++) {
        for(size_t j = 0; j < plugins[i].pi.sup_opts_len; j++){
            long_options[copied] = plugins[i].pi.sup_opts[j].opt;
            copied++;
        }
    }
    int option_index = 0;
    int choice;
    // Парсинг опций
    while ((choice = getopt_long(argc, argv, "vhP:OAN", long_options, &option_index)) != -1) {
        switch (choice) {
        case 0:
            // Обработка опций
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
            // Вывод справки
            printf("Usage: %s <options> <dir>\n", argv[0]);
            printf("<dir> - directory to search\n");
            printf("available options: -P <dir> to change plugin, -h for help, -A for 'and',   dir -O for 'or',  -N for 'not'\n");
            for(int i = 0; i < plug_cnt; i++){
                printf("Plugin purpose: %s\n", plugins[i].pi.plugin_purpose);
                for(size_t j = 0; j < plugins[i].pi.sup_opts_len; j++) printf("%s -- %s\n", plugins[i].pi.sup_opts[j].opt.name, plugins[i].pi.sup_opts[j].opt_descr);
                printf("\n");
            }
            // Освобождаем память и завершаем программу
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
            // Вывод информации о версии программы
            printf("Shurygin Danil N3245 Version 1.0\n");
            // Освобождаем память и завершаем программу
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
            // Смена плагина
            if(got_opts > 0){
                fprintf(stderr, "-P must be before plugin opts!\n");
                free(long_options); // Если мы уже нашли какую-то опцию до -P, это может вызвать проблемы
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

// Функция вывода информации о найденных файлах
void print_entry(int level, int type, const char *path) {
    if (!strcmp(path, ".") || !strcmp(path, "..") || type!=FTW_F)
        return; // Открываем только обычные файлы

    char indent[MAX_INDENT_LEVEL] = {0};
    memset(indent, ' ', MIN((size_t)level, MAX_INDENT_LEVEL));

    int cnt = 0;
    int cnt_success = 0;
    for(int i = 0; i < plug_cnt; i++){
        // Если не нашли опций у плагина, не запускаем из него функцию
        if(plugins[i].in_opts_len > 0){
            int tmp = plugins[i].ppf(path, plugins[i].in_opts, plugins[i].in_opts_len);
            if(tmp == -1){
                fprintf(stderr, "error in plugin! %s", strerror(errno));
                if(errno == EINVAL || errno == ERANGE) plugins[i].in_opts_len = 0;
            } else if (tmp == 0) cnt++;
            cnt_success++;
        }
    }
    
    // Проверяем соответствие условиям OR и NOT
    if((not && or && (cnt== 0)) || (not && !or && (cnt!=cnt_success))){
        printf("%sFound file: %s\n", indent, path);
    } else if((!not && or && (cnt > 0)) || (!not && !or && (cnt==cnt_success))){
        printf("%sFound file: %s\n", indent, path);
    }
    return;
} 

// Функция обхода директории
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
