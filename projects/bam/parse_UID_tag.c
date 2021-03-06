#include "utils.h"
#include "htslib/sam.h"
#include "htslib/kstring.h"
#include "htslib/bgzf.h"
// #include "htslib/hts.h"
#include <unistd.h>
#include "pkg_version.h"
#include "number.h"
#include "sequence.h"

int usage()
{
    fprintf(stderr,
            "Usage: sam_parse_uid in.bam\n"
            "   -tag BC       barcode tag for sam file\n"
            //"   -len INT   barcode length capped to this value\n"
            "   -start INT    start position of the UID sequence\n"
            "   -end   INT    end position of the UID sequence\n"
            "   -comp         complementary strand of UID sequence\n"
            "\n"
            "Version: %s\n"
            "Homepage: https://github.com/shiquan/small_projects\n"
            , PROJECTS_VERSION
            
        );
    return 1;
}

struct args {
    samFile *in;
    bam_hdr_t *header;
    const char *bc_tag;
    //int length;
    int start;
    int end;
    int barcode_compl;
} args = {
    .in = NULL,
    .header = NULL,
    .bc_tag = NULL,
    //.length = 0,
    .start = -1,
    .end = -1,
    .barcode_compl = 0,
};
int parse_args(int argc, char **argv)
{
    int i;
    const char *fn = NULL;
    //const char *length = NULL;
    const char *start = NULL;
    const char *end = NULL;
    for ( i = 1; i < argc; ) {
        const char *a = argv[i++];
        if ( strcmp(a, "-h") == 0 )
            return usage();

        const char **var = 0;
        if ( strcmp(a, "-tag") == 0 && args.bc_tag == NULL )
            var = &args.bc_tag;
        // else if ( strcmp(a, "-len") == 0 && args.length == 0 )
        // var = &length;
        else if ( strcmp(a, "-start") == 0 && start == NULL )
            var = &start;
        else if ( strcmp(a, "-end") == 0 && end == NULL )
            var = &end;
                
        if ( var != 0 ) {
            if ( i == argc) {
                error_print("Miss an argument after %s.", a);
                return 1;
            }
            *var = argv[i++];
            continue;
        }

        if ( strcmp(a, "-comp") == 0 ) {
            args.barcode_compl = 1;
            continue;
        }
        
        if ( fn == NULL )
            fn = a;
        else
            error("Unknown parameter %s.", a);        
    }

    if ( args.bc_tag == NULL )
        args.bc_tag = "BC";
    // todo: check the selfdefined tag is properly designed.

    /* if ( length ) */
    /*     args.length = str2int((char*)length); */

    if ( start ) {
        args.start = str2int((char*)start);
        args.start--; // convert 1 based to 0 based position
    }
    if ( end ) {
        args.end = str2int((char*)end);
        args.end--;
    }
    
    if ( fn == NULL ) {
        if (!isatty(fileno(stdin)) )
            args.in =sam_open("-", "r");
        else
            return usage();
    }
    
    args.in = sam_open(fn, "r");
    

    if ( args.in == 0 ) {
        error_print("Failed to open %s.", argc == 1 ? "-" : argv[1]);
        return 1;
    }

    if ( (args.header = sam_hdr_read(args.in)) == 0 ) {
        error_print("Failed to read the header.");
        return 1;
    }                    
    return 0;
}

int sam_parse_UID()
{
    fprintf(stdout, "%s", args.header->text);
    
    bam1_t *b = bam_init1();
    int r;
    kstring_t string = { 0, 0, 0};
    
    // read alignments
    while ( (r = sam_read1(args.in, args.header, b)) >= 0 ) {
        string.l = 0;
        if ( sam_format1(args.header, b, &string) == -1) 
            goto bad_format;
        int i;
        for ( i = 0; i < string.l-6; ++i ) {
            if ( string.s[i] == '\t')                 
                break;
            
            if ( string.s[i] == '_' && string.s[i+1] == 'U' && string.s[i+2] == 'I' && string.s[i+3] == 'D' && string.s[i+4] == ':') {
                int j;
                for ( j = i + 5; j < string.l -6; ++j )
                    if ( string.s[j] == '\t' )
                        break;
                int l = j - i - 5;
                char *p = string.s+i+5;
                if ( args.start > 0 ) {
                    p += args.start;
                    l -= args.start;
                }

                if ( args.end > 0 ) {
                    l = args.start > 0 ?  args.end - args.start + 1 : args.end + 1;
                }

                char *s = strndup(p, l);
                memmove(string.s+i, string.s+j, string.l -j + 1);
                string.l = string.l - ( j - i + 1);
                kputc('\t', &string);
                kputsn((char*)args.bc_tag, 2, &string); kputs(":Z:", &string);

                if ( args.barcode_compl ) {
                    char *r = rev_seqs(s, l);
                    free(s);
                    s = r;
                }
                    
                kputsn(s, l, &string);
                free(s);
                break;
            }
        }
        //kputc('\n', &string);
        puts(string.s);
    }
         
    if ( string.m ) free(string.s);
    bam_destroy1(b);
    // hts_close(fp);
    return 0;

  bad_format:
    if ( string.m ) free(string.s);
    bam_destroy1(b);
    //hts_close(fp);
    return 1;
}
void release_memory()
{
    bam_hdr_destroy(args.header);
    sam_close(args.in);
}
int main (int argc, char **argv)
{
    if ( parse_args ( argc, argv) )
        return 1;

    if ( sam_parse_UID() )
        return 1;

    release_memory();
    
    return 0;
}
