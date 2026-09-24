#define _XOPEN_SOURCE 700
#include "../src/core/core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void ok(Result r) { assert(r.code == RESULT_OK); }
static void ordered(const FileList *list, SortSettings sort) {
    for (size_t i=1;i<list->len;i++) assert(main_file_compare(&list->entries[i-1],&list->entries[i],sort)<=0);
}
int main(void) {
    FileInfo base[] = {
        {.name="Zdir",.path="Zdir",.valid=true,.kind=FILE_DIRECTORY,.size=2,.modified=9},
        {.name="a",.path="a",.valid=true,.kind=FILE_REGULAR,.size=UINT64_MAX,.modified=INT64_MIN},
        {.name="Link",.path="Link",.valid=true,.kind=FILE_LINK,.directory_target=true,.size=0,.modified=INT64_MAX},
        {.name="Adir",.path="Adir",.valid=true,.kind=FILE_DIRECTORY,.size=1,.modified=-2},
        {.name="A",.path="A",.valid=true,.kind=FILE_REGULAR,.size=UINT64_MAX,.modified=INT64_MIN},
        {.name="pipe",.path="pipe",.valid=true,.kind=FILE_OTHER,.size=5,.modified=0},
        {.name="0unknown",.path="0unknown",.kind=FILE_OTHER},
        {.name="b",.path="b",.valid=true,.kind=FILE_REGULAR,.size=0,.modified=INT64_MAX}
    };
    const unsigned expected[8][8] = {
        {3,0,6,4,1,7,2,5}, {0,3,5,2,7,1,4,6},
        {3,0,7,2,5,4,1,6}, {0,3,4,1,5,7,2,6},
        {3,0,4,1,5,7,2,6}, {0,3,7,2,5,4,1,6},
        {3,0,4,1,7,2,5,6}, {3,0,5,2,4,1,7,6}
    };
    for (int key=0;key<4;key++) for(int down=0;down<2;down++) {
        SortSettings sort={key,down}; FileInfo entries[8]; memcpy(entries,base,sizeof base);
        FileList list={entries,8}; main_list_sort(&list,sort);
        for (size_t i=0;i<8;i++) assert(!strcmp(entries[i].name,base[expected[key*2+down][i]].name));
        for (size_t a=0;a<8;a++) for(size_t b=0;b<8;b++) {
            int ab=main_file_compare(&base[a],&base[b],sort), ba=main_file_compare(&base[b],&base[a],sort);
            assert(ab==-ba && (a!=b || !ab));
            for(size_t c=0;c<8;c++) if(ab<=0 && main_file_compare(&base[b],&base[c],sort)<=0)
                assert(main_file_compare(&base[a],&base[c],sort)<=0);
        }
        FileList empty={0}; main_list_sort(&empty,sort);
        FileList one={entries,1}; main_list_sort(&one,sort);
        if(key) { FileInfo missing=base[6]; missing.kind=FILE_DIRECTORY;
            assert(main_file_compare(&missing,&base[1],sort)>0); }
        FileInfo decorated=base[1]; decorated.hidden=decorated.executable=true;
        assert(!main_file_compare(&base[1],&decorated,sort));
    }
    AppState empty_app={0}; size_t empty_selected=99;
    app_set_sort(&empty_app,(SortSettings){SORT_SIZE,true},&empty_selected);
    assert(empty_selected==0 && empty_app.sort.key==SORT_SIZE && empty_app.sort.descending);
    AppState single_app={.files={base,1}}; size_t single_selected=0;
    app_set_sort(&single_app,(SortSettings){SORT_MODIFIED,true},&single_selected);
    assert(single_selected==0 && single_app.files.entries==base);
    assert(!file_compare(&base[1],&base[4])); /* Shared picker/search comparator unchanged. */
    char root[]="/tmp/tfile-sort-XXXXXX"; assert(mkdtemp(root));
    const char *names[]={"a","A","x\n","x\\n","\xff","\\xFF","한글","e\314\201",".hidden"};
    for(size_t i=0;i<sizeof names/sizeof *names;i++) ok(core_create(root,names[i],false));
    ok(core_create(root,"dir",true)); char *dir=core_path_join(root,"dir"), *link=core_path_join(root,"alias");
    assert(!symlink(dir,link));
    AppState first,second; ok(app_init(&first,root)); ok(app_init(&second,root));
    FileList picker_before; ok(core_list(root,true,false,&picker_before));
    SearchResult before=core_search(root,"",search_default_limits(),NULL,NULL); ok(before.result);
    size_t history=first.history_len;
    for(size_t n=0;n<sizeof names/sizeof *names;n++) {
        size_t selected=0; while(strcmp(first.files.entries[selected].name,names[n])) selected++;
        for(int key=0;key<4;key++) for(int down=0;down<2;down++) {
            app_set_sort(&first,(SortSettings){key,down},&selected);
            assert(!strcmp(first.files.entries[selected].name,names[n])); ordered(&first.files,first.sort);
            assert(first.history_len==history && second.sort.key==SORT_NAME && !second.sort.descending);
            ordered(&second.files,second.sort);
        }
    }
    ok(app_navigate(&first,dir)); assert(first.sort.key==SORT_KIND && first.sort.descending);
    ok(app_history(&first,false)); ok(app_refresh(&first)); ordered(&first.files,first.sort);
    first.show_hidden=false; ok(app_refresh(&first));
    char *hidden=core_path_join(root,".hidden"); size_t selected=0; bool revealed;
    ok(app_open_search_result(&first,hidden,&selected,&revealed));
    assert(revealed && !strcmp(first.files.entries[selected].path,hidden)); ordered(&first.files,first.sort);
    FileInfo *saved=first.files.entries; size_t saved_history=first.history_len;
    assert(app_open_search_result(&first,"/nonexistent-tfile-sort-path",&selected,&revealed).code!=RESULT_OK);
    assert(first.files.entries==saved && first.history_len==saved_history && first.show_hidden && first.sort.descending);
    FileList picker_after; ok(core_list(root,true,false,&picker_after));
    assert(picker_before.len==picker_after.len);
    for(size_t i=0;i<picker_before.len;i++) assert(!strcmp(picker_before.entries[i].name,picker_after.entries[i].name));
    SearchResult after=core_search(root,"",search_default_limits(),NULL,NULL); ok(after.result);
    assert(before.matches.len==after.matches.len);
    for(size_t i=0;i<before.matches.len;i++) assert(!strcmp(before.matches.entries[i].path,after.matches.entries[i].path));
    file_list_free(&picker_before); file_list_free(&picker_after); search_result_free(&before); search_result_free(&after);
    app_free(&first); app_free(&second); free(dir); free(link); free(hidden); ok(core_delete(root));
    puts("PASS: eight sort orders, boundary values/comparator consistency, raw identity, independent app state, navigation/search and unchanged picker/search order");
}
