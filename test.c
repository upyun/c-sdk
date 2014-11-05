#include <string.h>
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "upyun.h"

//#define UPYUNCONF_BUCKET "bucketname"
//#define UPYUNCONF_USER "username"
//#define UPYUNCONF_PASS "password"
char* UPYUNCONF_BUCKET   = NULL;
char* UPYUNCONF_USER = NULL;
char* UPYUNCONF_PASS = NULL;
char* UPYUNCONF_DEBUG = NULL;

char PREFIX_PATH[200];

typedef struct local_file_s {
    long size;
    time_t date;
    char *path;
    char type;

    struct local_file_s* next;
    struct local_file_s* child;
} local_file_t;

typedef struct test_ctx_s {
    local_file_t *parent;
    local_file_t *root;
} test_ctx_t;

upyun_t *thiz = NULL;

size_t cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    (void)(userdata);

    char buf_1[10240] = {0};
    memcpy(buf_1, ptr, size * nmemb);
    printf("get data: %s", ptr);

    return size * nmemb;
}

size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
        int written = fwrite(ptr, size, nmemb, (FILE *)stream);
            return written;
}

local_file_t* new_file_info(test_ctx_t* ctx, const char* path, char type)
{
    local_file_t* file = calloc(1, sizeof(local_file_t));
    size_t parent_path_len = ctx->parent == ctx->root ?
        0 : strlen(ctx->parent->path);
    file->path = calloc(1, strlen(path) + parent_path_len + 2);

    file->type = type;

    if(ctx->parent != ctx->root)
        sprintf(file->path, "%s/%s", ctx->parent->path, path);
    else
        sprintf(file->path, "%s", path);

    char path1[4096] = {0};
    sprintf(path1, "input/%s", file->path);

    struct stat file_stat;
    stat(path1, &file_stat);
    file->size = file_stat.st_size;

    file->next = ctx->parent->child;
    ctx->parent->child = file;

    return file;
}

void test_readdir(test_ctx_t* ctx)
{
    (void)(ctx);

    upyun_dir_item_t* item = NULL;
    int status = 0;
    upyun_ret_e ret = upyun_read_dir(thiz, PREFIX_PATH, &item, &status);
    assert(ret == UPYUN_RET_OK && status == 200);

    printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
    upyun_dir_item_t* s_item = item;
    for(; item!=NULL; item=item->next)
    {
        printf("dir item: name %s, type %d, size %ld date %ld\n",
                item->file_name, item->file_type, item->file_size, item->date);
    }
    printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");

    upyun_dir_items_free(s_item);
}

void test_upload(test_ctx_t* ctx)
{
    upyun_ret_e ret = UPYUN_RET_OK;
    int status = 0;
    local_file_t* file = ctx->parent->child;

    char path[4096] = {0};

    for(; file!=NULL; file=file->next)
    {
        if(file->type == 'F')
        {
            sprintf(path, "%s%s", PREFIX_PATH, file->path);
            /* get the file_info check. */
            ret = upyun_get_fileinfo(thiz, path, NULL, &status);
            if(status == 200)
            {
                printf("duplicate file %s\n", file->path);
                continue;
            }

            sprintf(path, "input/%s", file->path);
            upyun_content_t content = {0};
            content.type = UPYUN_CONTENT_FILE;
            content.u.fp = fopen(path, "rb");
            content.len = file->size;
            content.md5 = 1;

            sprintf(path, "%s%s", PREFIX_PATH, file->path);
            ret = upyun_upload_file(thiz, path, &content, NULL, NULL, &status);
            fclose(content.u.fp);
            assert(ret == UPYUN_RET_OK && status == 200);

            /* get the file_info check. */
            upyun_file_info_t info;
            ret = upyun_get_fileinfo(thiz, path, &info, &status);
            assert(ret == UPYUN_RET_OK && status == 200);
            assert(strcmp(info.type, "file") == 0 && info.size == file->size);
        }
        else if(file->type == 'D')
        {
            /* make directory. */
            sprintf(path, "%s%s/", PREFIX_PATH, file->path);
            ret = upyun_make_dir(thiz, path, 0, &status);
            assert(ret == UPYUN_RET_OK && status == 200);

            /* get the file_info check. */
            upyun_file_info_t info;
            ret = upyun_get_fileinfo(thiz, path, &info, &status);
            assert(ret == UPYUN_RET_OK && status == 200);
            assert(strcmp(info.type, "folder") == 0 && info.size == -1);

            local_file_t* old_parent = ctx->parent;
            ctx->parent = file;
            test_upload(ctx);
            ctx->parent = old_parent;
        }
    }
}

void test_download(test_ctx_t* ctx)
{
    char path[4096] = {0};
    char path_download[4096] = {0};
    upyun_ret_e ret = UPYUN_RET_OK;
    int status = 0;
    local_file_t* file = ctx->parent->child;

    for(; file!=NULL; file=file->next)
    {
        if(file->type == 'F')
        {
            sprintf(path, "%s%s", PREFIX_PATH, file->path);

            sprintf(path_download, "output/%s", file->path);
            FILE* fp = NULL;
            assert((fp=fopen(path_download, "w")) != NULL);

            ret = upyun_download_file(thiz, path,
                                      (UPYUN_CONTENT_CALLBACK)write_data,
                                      (void* )fp,
                                      &status);
            fclose(fp);
            assert(ret == UPYUN_RET_OK && status == 200);
        }
        else if(file->type == 'D')
        {
            sprintf(path_download, "output/%s", file->path);
            if(access(path_download, F_OK) <  0)
            {
                mkdir(path_download, 0777);
            }

            local_file_t* old_parent = ctx->parent;
            ctx->parent = file;
            test_download(ctx);
            ctx->parent = old_parent;
        }
    }
}

void test_delete(test_ctx_t* ctx, int assert)
{
    char path[4096] = {0};
    upyun_ret_e ret = UPYUN_RET_OK;
    int status = 0;
    local_file_t* file = ctx->parent->child;

    for(; file!=NULL; file=file->next)
    {
        if(file->type == 'F')
        {
            sprintf(path, "%s%s", PREFIX_PATH, file->path);
            ret = upyun_get_fileinfo(thiz, path, NULL, &status);
            if(status == 404)
            {
                continue;
            }

            ret = upyun_remove_file(thiz, path, &status);
            if(assert) assert(ret == UPYUN_RET_OK && status == 200);
        }
        else if(file->type == 'D')
        {
            local_file_t* old_parent = ctx->parent;
            ctx->parent = file;
            test_delete(ctx, assert);
            ctx->parent = old_parent;

            sprintf(path, "%s%s/", PREFIX_PATH, file->path);

            ret = upyun_remove_file(thiz, path, &status);
            if(assert) assert(ret == UPYUN_RET_OK && status == 200);
        }
    }
}

static void list_dir(test_ctx_t* ctx)
{
    DIR              *pDir;
    struct dirent    *ent;
    char path[4096] = {0};

    if(ctx->parent == ctx->root)
        sprintf(path, "input");
    else
        sprintf(path, "input/%s", ctx->parent->path);

    pDir = opendir(path);

    while((ent=readdir(pDir))!=NULL)
    {
        if(strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,"..")==0)
            continue;

        if(ent->d_type & DT_DIR)
        {

            local_file_t* new_file = new_file_info(ctx, ent->d_name, 'D');
            local_file_t* old_parent = ctx->parent;
            ctx->parent = new_file;
            list_dir(ctx);
            ctx->parent = old_parent;
        }
        else
        {
            new_file_info(ctx, ent->d_name, 'F');
        }
    }
}

static void display_dir(test_ctx_t* ctx)
{
    local_file_t* file = ctx->parent->child;

    for(; file!=NULL; file=file->next)
    {
        if(file->type == 'F')
        {
            printf("file: %s, size: %ld\n", file->path, file->size);

        }
        else if(file->type == 'D')
        {
            local_file_t* old_parent = ctx->parent;
            printf("dir: %s\n", file->path);

            ctx->parent = file;
            display_dir(ctx);
            ctx->parent = old_parent;
        }
    }
}

void test(test_ctx_t *ctx)
{
    ctx->root = calloc(1, sizeof(local_file_t));
    ctx->root->path = "";
    ctx->parent = ctx->root;

    list_dir(ctx);
    ctx->parent = ctx->root;
    display_dir(ctx);

    test_delete(ctx, 0);

    printf("test upload......................................\n");
    test_upload(ctx);
    printf("test download....................................\n");
    if(access("./output", F_OK) <  0)
    {
        mkdir("./output", 0777);
    }

    test_download(ctx);
    printf("test readdir.....................................\n");
    test_readdir(ctx);
    printf("test delete......................................\n");
    test_delete(ctx, 1);

    upyun_ret_e ret = UPYUN_RET_OK;
    int status = 0;
    upyun_dir_item_t* item = NULL;
    upyun_read_dir(thiz, PREFIX_PATH, &item, &status);
    assert(ret == UPYUN_RET_OK && status == 200);
    assert(item == NULL);
}

int main(void)
{
    UPYUNCONF_USER = getenv("UPYUN_USERNAME");
    UPYUNCONF_PASS = getenv("UPYUN_PASSWORD");
    UPYUNCONF_BUCKET = getenv("UPYUN_BUCKET");
    UPYUNCONF_DEBUG = getenv("UPYUN_DEBUG");
    sprintf(PREFIX_PATH, "/%s/", UPYUNCONF_BUCKET);

    upyun_global_init();
    upyun_config_t conf = {0};
    conf.user = UPYUNCONF_USER;
    conf.passwd = UPYUNCONF_PASS;
    conf.endpoint = UPYUN_ED_AUTO;
    conf.debug = UPYUNCONF_DEBUG == NULL ? 0 : 1;

    upyun_t *u = upyun_create(&conf);

    test_ctx_t ctx = {0};
    thiz = u;
    test(&ctx);

    upyun_destroy(u);
    upyun_global_cleanup();

    return 0;
}
