
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>

#include "plugin_api.h"

static char *g_purpose = "Check if file contains specified bytes in any order";
static char *g_author = "Belyakov Nikita";

static struct plugin_option g_options[] = {
    {
        {"bytes", required_argument, 0, 0},
        "Bytes to search for"
    }
};

static int g_options_len = sizeof(g_options) / sizeof(g_options[0]);

int plugin_get_info(struct plugin_info *ppi)
{
    if (!ppi)
    {
        fprintf(stderr, "ERROR: Invalid argument\n");
        return -1;
    }

    ppi->plugin_purpose = g_purpose;
    ppi->plugin_author = g_author;
    ppi->sup_opts_len = g_options_len;
    ppi->sup_opts = g_options;

    return 0;
}

int plugin_process_file(const char *fname,
                        struct option in_opts[],
                        size_t in_opts_len)
{
    if (!fname || !in_opts || !in_opts_len)
    {
        errno = EINVAL;
        return -1;
    }

    char *bytes_string = NULL;
    for (size_t i = 0; i < in_opts_len; i++)
    {
        if (!strcmp(in_opts[i].name, "bytes"))
        {
            bytes_string = strdup((char *)in_opts[i].flag);
        }
        else
        {
            errno = EINVAL;
            return -1;
        }
    }
    if (!bytes_string)
    {
        errno = EINVAL;
        return -1;
    }

    unsigned char *bytes = NULL;
    size_t bytes_cnt = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(bytes_string, ",", &saveptr);
    while (tok != NULL) {
        if (strlen(tok) > 2 && tok[0] == '0' && tok[1] == 'b') {
            if (strlen(tok) > 10) {
                errno = ERANGE;
                if (bytes_string)
                    free(bytes_string);
                if (bytes)
                    free(bytes);
                return -1;
            }
            bytes = realloc(bytes, sizeof(unsigned char) * (bytes_cnt + 1));
            bytes[bytes_cnt] = 0;
            for (size_t i = strlen(tok) - 1; i > 1; i--) {
                if (tok[i] == '1') {
                    bytes[bytes_cnt] |= (1 << (strlen(tok) - i - 1));
                }
            }
            
            bytes_cnt++;
        }
        else if (strlen(tok) > 2 && tok[0] == '0' && tok[1] == 'x') {
            if(strlen(tok) > 4){
                errno = ERANGE;
                if (bytes_string)
                    free(bytes_string);
                if (bytes)
                    free(bytes);
                return -1;
            }
            bytes = realloc(bytes, sizeof(unsigned char) * (bytes_cnt + 1));
            bytes[bytes_cnt] = 0;
            sscanf(tok+2, "%02hhx",bytes+bytes_cnt);
            
            bytes_cnt++;
        }
        else
        {
            if(tok != NULL && tok[0] == '0' && strcmp(tok, "0")!= 0){
                errno = EINVAL;
                if (bytes_string)
                    free(bytes_string);
                if (bytes)
                    free(bytes);
                return -1;
            }   
            char *endptr;
            long num = strtol(tok, &endptr, 10);
            if(num == 0 && endptr == tok && strcmp(tok, "0") != 0){
                errno = EINVAL;
                if (bytes_string)
                    free(bytes_string);
                if (bytes)
                    free(bytes);
                return -1;
            }
            if(num > 255){
                errno = ERANGE;
                 if (bytes_string)
                    free(bytes_string);
                if (bytes)
                    free(bytes);
                return -1;
            } else{
                bytes = realloc(bytes, (bytes_cnt+1) * sizeof(unsigned char));
                bytes[bytes_cnt] = (unsigned char)num;
                
                
                bytes_cnt++;
            }
        }
        tok = strtok_r(NULL, ",", &saveptr);
    }

    int *found = calloc(sizeof(int), bytes_cnt);
    FILE *f = fopen(fname, "rb");
    if(!f){
        fprintf(stderr, "fopen() failed:%s\n", strerror(errno));
        if(found) 
                free(found);
            if (bytes_string)
                free(bytes_string);
            if (bytes)
                free(bytes);
        return -1;
    }
    while(!feof(f)){
        unsigned char buf[1];
        size_t t = fread(buf, sizeof(unsigned char), 1, f);
        if(t != 1 && !feof(f)) {
            if(found) 
                free(found);
            if (bytes_string)
                free(bytes_string);
            if (bytes)
                free(bytes);
            fclose(f);
            return -1;
        } else if (t == 0) break;
        for(size_t i = 0; i < bytes_cnt; i++){
            if(memcmp((char*)bytes+i, (char*)buf, 1) == 0){
                found[i]++;


            }
        }
    }
    int ret = 0;
    for(size_t i = 0; i < bytes_cnt; i++){
        if(found[i] == 0) ret = 1;
    }
    if(getenv("LAB1DEBUG")!=NULL && ret == 0){
        fprintf(stderr,"Debug mode: Target bytes (");
        for(size_t i = 0; i < bytes_cnt; i++) fprintf(stderr, "%d,", (int)bytes[i]);
        fprintf(stderr, ") found in file %s!\n", fname);
    }
    if(found) free(found);
    if (bytes_string)
        free(bytes_string);
    if (bytes)
        free(bytes);
    fclose(f);
    return ret;
}
