#ifndef XML_PARSE_H
#define XML_PARSE_H

#include <stddef.h>

/* Fixed bounds. The parser never allocates a node list and never grows with
   the document: input is pulled through a fixed window, and the only nesting
   state is a fixed-depth stack of the elements that are still open.
   CDATA bodies are emitted as text in those same fixed chunks, and a text
   chunk never ends inside a UTF-8 sequence.

   In text and attribute values the five predefined entities (&amp; &lt;
   &gt; &quot; &apos;) and decimal or hexadecimal character references
   (&#N; and &#xH; with a lowercase x) are decoded to UTF-8. A reference to a non-XML Char, a
   malformed or unterminated reference, and any other entity are errors;
   CDATA keeps reference spelling literally. Every consumed byte, including
   comments, PIs, the skipped DTD and content after the root, must be
   well-formed UTF-8 encoding an XML Char.

   The XML declaration must open the document (after an optional UTF-8
   BOM) with ordered, unique version="1.0", encoding="UTF-8" and
   standalone pseudo-attributes. At most one DOCTYPE is accepted before the
   root; its internal subset is skipped with comment/PI/quote awareness, and
   neither the external subset nor any declared entity is ever loaded or
   expanded. */

#define XML_NAME_MAX 64
#define XML_ATTR_MAX 8
#define XML_ATTR_VAL_MAX 256
#define XML_TEXT_MAX 1024
#define XML_MAX_DEPTH 64
#define XML_BUF_SIZE 4096

typedef enum {
    XML_EV_NONE = 0,
    XML_EV_START = 1,
    XML_EV_END = 2,
    XML_EV_TEXT = 3,
    XML_EV_EOF = 4,
    XML_EV_ERROR = 5
} XmlEventKind;

typedef struct {
    XmlEventKind kind;
    char name[XML_NAME_MAX];
    int nattr;
    char attr_name[XML_ATTR_MAX][XML_NAME_MAX];
    char attr_val[XML_ATTR_MAX][XML_ATTR_VAL_MAX];
    char text[XML_TEXT_MAX];
    size_t text_len;
    int from_cdata;
    size_t byte_offset;
    char error[160];
} XmlEvent;

/* Read up to max bytes into dst. Return the count, 0 on EOF, <0 on error.
   A short read is not EOF; the parser asks again. */
typedef int (*XmlReadFn)(void *user, char *dst, int max);

typedef struct XmlParser {
    XmlReadFn read;
    void *user;
    char buf[XML_BUF_SIZE];
    size_t len;
    size_t pos;
    int eof;
    int depth;
    int pending_end;
    int failed;
    int done;
    int bom_checked;
    int in_cdata;
    int cdata_mark;
    int seen_root;
    int seen_doctype;
    int document_start;
    int bracket_run;
    int utf8_need;
    int utf8_bad;
    unsigned utf8_lead;
    unsigned utf8_cp;
    size_t offset;
    char end_name[XML_NAME_MAX];
    char stack[XML_MAX_DEPTH][XML_NAME_MAX];
    char errmsg[160];
} XmlParser;

int xml_parser_init(XmlParser *p, XmlReadFn read, void *user);
int xml_parser_init_file(XmlParser *p, void *file);
int xml_next(XmlParser *p, XmlEvent *ev);

#endif
