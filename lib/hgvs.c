#include "utils.h"
#include "genepred.h"

struct hgvs_spec {
    struct genepred_spec *data;
    struct hgvs_des des;
} spec;

void hgvs_core_clear(struct hgvs_core *core)
{
    if ( core->name.name1 )
        free(core->name.name1);
    if ( core->name.name2 )
        free(core->name.name2);
    memset(core, 0, sizeof(struct hgvs_core));
}
void hgvs_des_clear(struct hgvs_des *des)
{
    int i;
    for ( i = 0; i < des->i; ++i) {
        hgvs_core_clear(&des->a[i]);
    }
    free(des->a);
    if ( des->chrom )
        free(des->chrom);
    if ( des->ref_length )
        free(des->ref);
    if ( des->alt_length )
        free(des->alt);
    memset(des, 0, sizeof(struct hgvs_des));
}

int init_hgvs_spec(const char *fname, const char *fasta)
{
    memset(&spec, 0, sizeof(struct hgvs_spec));
    spec.data = genepred_spec_init();
    if ( genepred_load_data(spec.data, fname) == NULL )
        return 1;
    if ( genepred_load_fasta(spec.data, fasta) == NULL )
        return 1;
    
    return 0;
}
// Standard HGVS name should be NM_0001.2:c.123A>G; tolerant format could be NM_0001:c.123A>G (no version number);
// 
int check_hgvs_name(char *name)
{
}
int parse_hgvs_name(char *name)
{
    if ( check_hgvs_name(name) )
        return 1;

    return 0;
}
// return the exon id, for intergenic return -1.
int find_the_block(struct genepred_line *line, int *blk_start, int *blk_end, int pos)
{
    *blk_start = -1;
    *blk_end = -1;
    int i;
    for ( i = 0; i < line->exoncount; ++i ) {
        int start = line->exons[BLOCK_START][i];
        int end = line->exons[BLOCK_END][i];
        if ( pos < start) {
            *blk_end = i;
            break;
        } else {
            *blk_start = i;
            if ( pos <= end ) {
                *blk_end = i;
                break;
            }           
        }
    }
    // Always return 0 ? if out of range return 1??
    return 0;
}
int find_locate(struct genepred_line *line, int *pos, int *offset, int start)
{
    int i;
    int blk_start = 0;
    int blk_end = 0;
    for (i = 0; i < line->exoncount; ++i) {        
        if ( find_the_block(line, &blk_start, &blk_end, start ) )
            return 1;
        // Exon.
        if ( blk_start == blk_end ) {

            int offset = start - line->exons[BLOCK_START][blk_start];
            int back_offset = line->exons[BLOCK_END][blk_start] - start;
            
            int offset1 = read_loc(line->dna_ref_offsets[BLOCK_START][blk_start]);
            int offset2 = read_loc(line->dna_ref_offsets[BLOCK_END][blk_start]);
            int type1 = read_type(line->dna_ref_offsets[BLOCK_START][blk_start]);
            int type2 = read_type(line->dna_ref_offsets[BLOCK_END][blk_start]);
            int pos1;
            int pos2; 
            if ( type1 == type2 ) {
                pos1 = offset1 - offset;
                pos2 = offset2 + back_offset;
                if ( pos1 != pos2 ) {
                    pos1 = offset1 + offset;
                    pos2 = offset2 - back_offset;
                    assert(pos1 == pos2);
                }
            } else {
                pos1 = offset1 - offset;
                pos2 = offset2 - back_offset;
            }
            
            if ( pos1 > 0 && pos2 > 0 ) {
                *pos = compact_loc(offset1 + offset, type1);
                break;
            }
            if ( pos1 < 0 ) {
                if ( type2 ==  REG_CODING && strand == '-' )
                    pos2 = offset2 + back_offset;
                if ( pos2 > 0 ) {
                    *pos = compact_loc(pos2, type2);
                } else {
                    int posi = strand == '+' ? 1 - pos1 : 1 - pos2;
                    *pos = compact_loc(posi, REG_CODING);
                }
                break;
            }

            if ( pos2 < 0 ) {
                if ( type1 == REG_CODING && strand == '+')
                    pos1 = offset1 + offset;
                *pos = compact_loc(pos1, type1);
            }                
            *offset = 0;
            
        } else {
            // Intron.
            if (blk_start == -1 ) {
                // only happens at the upstream of gene region
                assert(blk_end == 0 );
                *pos = line->dna_ref_offsets[BLOCK_START][blk_end];
                *offset = start - line->exons[BLOCK_START][blk_end];
                break;
            }
            if (blk_end == -1) {
                // happens at the downstream of gene region
                assert(blk_start == line->exoncount -1 );
                *pos = line->dna_ref_offsets[BLOCK_END][blk_start];
                *offset = start - line->exons[BLOCK_END][blk_end];
                break;
            }
            
            int offset1 = start - line->exons[BLOCK_END][blk_start];
            int offset2 = line->exons[BLOCK_START][blk_end] - start;
            if ( offset1 > offset2 ) {
                *pos = line->dna_ref_offsets[BLOCK_START][blk_end];
                *offset = -1 * offset2;
            } else {
                *pos = line->dna_ref_offsets[BLOCK_END][blk_start];
                *offset = offset1;
            } 
        }        
    }
    return 0;
}
// return 0 on success, 1 on out of range.
int generate_hgvs_core(struct genepred_line *line, struct hgvs_core *core, int start, int end)
{
    int i;
    int blk_start = 0, blk_end = 0;
    struct hgvs_name *name = &core->name;
    if ( line->loc_parsed == 0 )
        parse_line_locs(line);

    if ( find_locate(line, &name->pos, &name->offset, start) )
        return 1;
    // Locate end. For most cases variants are snps, start == end.
    if ( end != start ) {
        find_locate(line, &name->end_pos, &name->end_offset, end);
    }
    name->name1 = strdup(line->name1);
    name->name2 = strdup(line->name2);        
    return 0;
}
// Fill all possible HGVS names for this variant.
int fill_hgvs_name()
{
    // Check the position inited.
    struct hgvs_des *des = &spec.des;
    if ( des->start == 0 )
        error("Variant position is not inited.");

    struct genepred_line *line = genepred_retrieve_region(spec->data, des->name, des->start, des->end);

    for ( ;; ) {
        if ( line == NULL )
            break;
        
        if ( des->l == des->m ) {
            des->m += 2;
            des->a = (struct hgvs_core*)realloc(des->a, des->m*sizeof(struct hgvs_core));
        }

        hgvs_core_clear(&des->a[des->l]);
        if ( generate_hgvs_core(line, &des->a[des->l]) == 0 )
            des->l ++;
        struct genepred_line * temp = line;
        line = line->next;
        genepred_line_destroy(temp);
    }
    return 0;
}

#ifdef HGVS_MAIN
int usage()
{
    fprintf(stderr,
            "Usage: hgvs_converter NM001:c.123A>T\n"
        );
    return 1;
}
int parse_args(int ac, char **av)
{
}

void convert_hgvs()
{
}
void release_memory()
{
}
int main(int argc, char **argv)
{
    if ( parse_args(--argc, ++argv) )
        return 1;
    convert_hgvs();
    release_memory();
    return 0;
}
#endif
