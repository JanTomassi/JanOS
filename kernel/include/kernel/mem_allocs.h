#pragma once

struct mem_malloc_tag;
struct mem_phy_mem_link;
struct mem_phy_mem_tag;

typedef struct mem_malloc_tag mem_malloc_tag_t;
typedef struct mem_phy_mem_link mem_phy_mem_link_t;
typedef struct mem_phy_mem_tag mem_phy_mem_tag_t;

// Insert phy_mem_tag sorted by ptr in the chain
void insert_phy_mem_tag(mem_phy_mem_tag_t *tag, struct list_head *chain);
// Give the phy_mem_tag back to free_phy_tags_list
void give_phy_mem_tag(mem_phy_mem_tag_t *);
// Get the phy_mem_tag from free_phy_tags_list
mem_phy_mem_tag_t *get_phy_mem_tag();

// Give phy_mem_link* back to free_phy_links_list
void give_phy_mem_link(mem_phy_mem_link_t *);
// Get phy_mem_link* from free_phy_links_list
mem_phy_mem_link_t *get_phy_mem_link();
// Get the phy mem tag from a list of phy_mem_link
mem_phy_mem_tag_t *get_phy_tag_from_link(struct list_head *link);

// Insert malloc_tag sorted by ptr in the chain
void insert_tag(mem_malloc_tag_t *tag, struct list_head *list);
// Give malloc_tag* back to free_tags_list
void give_tag(mem_malloc_tag_t *);
// Get malloc_tag* from free_tags_list
mem_malloc_tag_t *get_tag();
