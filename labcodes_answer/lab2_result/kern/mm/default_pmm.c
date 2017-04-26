#include <pmm.h>
#include <list.h>
#include <string.h>
#include <default_pmm.h>

/* In the first fit algorithm, the allocator keeps a list of free blocks (known as the free list) and,
   on receiving a request for memory, scans along the list for the first block that is large enough to
   satisfy the request. If the chosen block is significantly larger than that requested, then it is 
   usually split, and the remainder added to the list as another free block.
   Please see Page 196~198, Section 8.2 of Yan Wei Min's chinese book "Data Structure -- C programming language"
*/
// LAB2 EXERCISE 1: YOUR CODE
// you should rewrite functions: default_init,default_init_memmap,default_alloc_pages, default_free_pages.
/*
 * Details of FFMA
 * (1) Prepare: In order to implement the First-Fit Mem Alloc (FFMA), we should manage the free mem block use some list.
 *              The struct free_area_t is used for the management of free mem blocks. At first you should
 *              be familiar to the struct list in list.h. struct list is a simple doubly linked list implementation.
 *              You should know howto USE: list_init, list_add(list_add_after), list_add_before, list_del, list_next, list_prev
 *              Another tricky method is to transform a general list struct to a special struct (such as struct page):
 *              you can find some MACRO: le2page (in memlayout.h), (in future labs: le2vma (in vmm.h), le2proc (in proc.h),etc.)
 * (2) default_init: you can reuse the  demo default_init fun to init the free_list and set nr_free to 0.
 *              free_list is used to record the free mem blocks. nr_free is the total number for free mem blocks.
 * (3) default_init_memmap:  CALL GRAPH: kern_init --> pmm_init-->page_init-->init_memmap--> pmm_manager->init_memmap
 *              This fun is used to init a free block (with parameter: addr_base, page_number).
 *              First you should init each page (in memlayout.h) in this free block, include:
 *                  p->flags should be set bit PG_property (means this page is valid. In pmm_init fun (in pmm.c),
 *                  the bit PG_reserved is setted in p->flags)
 *                  if this page  is free and is not the first page of free block, p->property should be set to 0.
 *                  if this page  is free and is the first page of free block, p->property should be set to total num of block.
 *                  p->ref should be 0, because now p is free and no reference.
 *                  We can use p->page_link to link this page to free_list, (such as: list_add_before(&free_list, &(p->page_link)); )
 *              Finally, we should sum the number of free mem block: nr_free+=n
 * (4) default_alloc_pages: search find a first free block (block size >=n) in free list and reszie the free block, return the addr
 *              of malloced block.
 *              (4.1) So you should search freelist like this:
 *                       list_entry_t le = &free_list;
 *                       while((le=list_next(le)) != &free_list) {
 *                       ....
 *                 (4.1.1) In while loop, get the struct page and check the p->property (record the num of free block) >=n?
 *                       struct Page *p = le2page(le, page_link);
 *                       if(p->property >= n){ ...
 *                 (4.1.2) If we find this p, then it' means we find a free block(block size >=n), and the first n pages can be malloced.
 *                     Some flag bits of this page should be setted: PG_reserved =1, PG_property =0
 *                     unlink the pages from free_list
 *                     (4.1.2.1) If (p->property >n), we should re-caluclate number of the the rest of this free block,
 *                           (such as: le2page(le,page_link))->property = p->property - n;)
 *                 (4.1.3)  re-caluclate nr_free (number of the the rest of all free block)
 *                 (4.1.4)  return p
 *               (4.2) If we can not find a free block (block size >=n), then return NULL
 * (5) default_free_pages: relink the pages into  free list, maybe merge small free blocks into big free blocks.
 *               (5.1) according the base addr of withdrawed blocks, search free list, find the correct position
 *                     (from low to high addr), and insert the pages. (may use list_next, le2page, list_add_before)
 *               (5.2) reset the fields of pages, such as p->ref, p->flags (PageProperty)
 *               (5.3) try to merge low addr or high addr blocks. Notice: should change some pages's p->property correctly.
 */
free_area_t free_area; // 一个记录空闲页帧的双向链表

#define free_list (free_area.free_list) // 链表头
#define nr_free (free_area.nr_free) // 链表中空闲页帧的个数

static void
default_init(void) {
    list_init(&free_list); // 初始化链表
    nr_free = 0; // 空闲页帧个数初始化为0
}

static void
default_init_memmap(struct Page *base, size_t n) { // 初始化一个空闲块（空闲块第一个页帧的起始地址，页帧的个数）
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) { // 初始化空闲块中的每个页帧
        assert(PageReserved(p));
        p->flags = 0;
        SetPageProperty(p); // 设置该页帧为可用
        p->property = 0; // 将页帧的property属性设为0，第一个页帧的此属性在后面将被重设为n
        set_page_ref(p, 0); // 将页帧的ref属性设为0，表示该页帧被页表的引用次数
        list_add_before(&free_list, &(p->page_link)); // 将该页帧加入到free_list为链表头的双向链表中，位于首位
    }
    nr_free += n; // 该链表链接的空闲页帧个数加n
    //first block // 应该是“first page”，原注释写错了
    base->property = n; // 这是空闲块的第一个页帧，故它的property属性记录该空闲块的页帧个数
}

static struct Page *
default_alloc_pages(size_t n) { // 按First fit算法分配物理页帧（分配n个页帧）
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }
    list_entry_t *le, *len;
    le = &free_list;

    while((le=list_next(le)) != &free_list) { // 将链表遍历一遍
      struct Page *p = le2page(le, page_link); // 将le指向的链表结构转换为Page结构
      if(p->property >= n){ // 找到了可以足够分配的第一个空闲块
        int i;
        for(i=0;i<n;i++){ // 对找到的这个空闲块的前n个页帧进行设置和删除
          len = list_next(le); // len为le的下一个页帧
          struct Page *pp = le2page(le, page_link);
          SetPageReserved(pp); // 设置该页帧为被使用了
          ClearPageProperty(pp); // 清除该页帧的可用标志
          list_del(le); // 将当前处理的页帧从链表中删除
          le = len;
        }
        if(p->property>n){ // 该空闲块分配完n个页帧后还有剩余
          (le2page(le,page_link))->property = p->property - n; // 设置剩余的空闲块的大小
        }
        ClearPageProperty(p);
        SetPageReserved(p);
        nr_free -= n; // 更新链表中全部空闲块总共的页帧个数
        return p; // 找到了，返回所分配页帧的首页指针，以此为起始点连续n个页帧为所分配页帧
      }
    }
    return NULL; // 遍历一遍链表后仍没有找到一个足够分配n个页帧的空闲块
}

static void
default_free_pages(struct Page *base, size_t n) { // 释放n个页帧
    assert(n > 0);
    assert(PageReserved(base));

    list_entry_t *le = &free_list; // 定义变量le指向空闲块链表
    struct Page * p;
    while((le=list_next(le)) != &free_list) { // 遍历链表
      p = le2page(le, page_link);
      if(p>base){ // 在链表中找到了插入所释放页帧的正确位置（按地址从小到大放置页帧）
        break;
      }
    }
    //list_add_before(le, base->page_link);
    for(p=base;p<base+n;p++){ // 将所释放的页帧逐个插入链表
      list_add_before(le, &(p->page_link));
    }
    base->flags = 0;
    set_page_ref(base, 0);
    ClearPageProperty(base);
    SetPageProperty(base);
    base->property = n; // 新插入链表的空闲块大小为n个页帧
    
    p = le2page(le,page_link) ;
    if( base+n == p ){ // 如果刚插入的空闲块刚好与后面相邻的空闲块物理地址是连续的，则合并
      base->property += p->property; // 设置合并后的空闲块大小
      p->property = 0; // 合并后，原相邻的空闲块首页的property属性设为0，因它不再是首页了
    }
    le = list_prev(&(base->page_link));
    p = le2page(le, page_link);
    if(le!=&free_list && p==base-1){ // 如果刚插入的空闲块也刚好与前面相邻的空闲块物理地址是连续的，则也合并
      while(le!=&free_list){ // 向前遍历链表，目的是找到前面相邻空闲块的首个页帧，以更改合并后新空闲块大小
        if(p->property){ // 找到了首页帧
          p->property += base->property; // 更改合并后的空闲块大小
          base->property = 0; // 该页帧已不再是空闲块的首页了，故设置property为0
          break;
        }
        le = list_prev(le);
        p = le2page(le,page_link);
      }
    }

    nr_free += n; // 更改空闲块的总页帧数
    return ;
}

static size_t
default_nr_free_pages(void) {
    return nr_free;
}

static void
basic_check(void) {
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));

    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    assert(alloc_page() == NULL);

    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(nr_free == 3);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(alloc_page() == NULL);

    free_page(p0);
    assert(!list_empty(&free_list));

    struct Page *p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);

    assert(nr_free == 0);
    free_list = free_list_store;
    nr_free = nr_free_store;

    free_page(p);
    free_page(p1);
    free_page(p2);
}

// LAB2: below code is used to check the first fit allocation algorithm (your EXERCISE 1) 
// NOTICE: You SHOULD NOT CHANGE basic_check, default_check functions!
static void
default_check(void) {
    int count = 0, total = 0;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count ++, total += p->property;
    }
    assert(total == nr_free_pages());

    basic_check();

    struct Page *p0 = alloc_pages(5), *p1, *p2;
    assert(p0 != NULL);
    assert(!PageProperty(p0));

    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));
    assert(alloc_page() == NULL);

    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    free_pages(p0 + 2, 3);
    assert(alloc_pages(4) == NULL);
    assert(PageProperty(p0 + 2) && p0[2].property == 3);
    assert((p1 = alloc_pages(3)) != NULL);
    assert(alloc_page() == NULL);
    assert(p0 + 2 == p1);

    p2 = p0 + 1;
    free_page(p0);
    free_pages(p1, 3);
    assert(PageProperty(p0) && p0->property == 1);
    assert(PageProperty(p1) && p1->property == 3);

    assert((p0 = alloc_page()) == p2 - 1);
    free_page(p0);
    assert((p0 = alloc_pages(2)) == p2 + 1);

    free_pages(p0, 2);
    free_page(p2);

    assert((p0 = alloc_pages(5)) != NULL);
    assert(alloc_page() == NULL);

    assert(nr_free == 0);
    nr_free = nr_free_store;

    free_list = free_list_store;
    free_pages(p0, 5);

    le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        count --, total -= p->property;
    }
    assert(count == 0);
    assert(total == 0);
}

const struct pmm_manager default_pmm_manager = {
    .name = "default_pmm_manager",
    .init = default_init,
    .init_memmap = default_init_memmap,
    .alloc_pages = default_alloc_pages,
    .free_pages = default_free_pages,
    .nr_free_pages = default_nr_free_pages,
    .check = default_check,
};

