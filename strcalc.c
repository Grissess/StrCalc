#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

typedef enum {
    TOK_STR,
    TOK_OP,
    TOK_EOF
} toktype_t;

typedef struct {
    char *str;
    size_t len;
} buffer_t;

typedef struct {
    toktype_t type;
    union {
        char op;
        struct {
            buffer_t buf;
            size_t cap;
        };
    };
} tok_t;

void err(char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, "\n");
}
    
#define fatal(...) do { err(__VA_ARGS__); exit(EXIT_FAILURE); } while(0)

int next_tok(tok_t *tok, FILE *in) {
    int c;
loop:
    c = fgetc(in);
    switch(c) {
        case EOF:
            tok->type = TOK_EOF;
            return 0;
            break;
            
        case '(': case ')': case '.': case '^':
            tok->type = TOK_OP;
            tok->op = c;
            return 0;
            break;
            
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            tok->type = TOK_STR;
            tok->buf.str = malloc(sizeof(char)*8);
            tok->cap = 8;
            tok->buf.str[0] = c;
            tok->buf.len = 1;
            while(isdigit(c = fgetc(in))) {
                tok->buf.str[tok->buf.len++] = c;
                if(tok->buf.len >= tok->cap) {
                    tok->cap <<= 1;
                    tok->buf.str = realloc(tok->buf.str, tok->cap);
                }
            }
            ungetc(c, in);
            return 0;
            break;
            
        case ' ': case '\t': case '\b': case '\v': case '\r': case '\n':
            goto loop;
            break;
            
        default:
            err("Ignoring unrecognized character '%c' in input", c);
            goto loop;
            break;
    }
    err("Control fell off next_tok without a return");
    return 1;
}

buffer_t buf_new(char *s, size_t sz) {
    buffer_t res;
    res.str = malloc(sizeof(char)*sz);
    memcpy(res.str, s, sizeof(char)*sz);
    res.len = sz;
    return res;
}

buffer_t buf_dup(buffer_t buf) {
    buffer_t res;
    res.str = malloc(sizeof(char)*buf.len);
    memcpy(res.str, buf.str, buf.len);
    res.len = buf.len;
    return res;
}

void buf_print(buffer_t buf) {
    size_t i;
    char *p = buf.str;
    for(i=0; i<buf.len; i++) putchar(*(p++));
}

buffer_t buf_concat(buffer_t a, buffer_t b) {
    buffer_t res;
    res.str = malloc(sizeof(char)*(a.len+b.len));
    memcpy(res.str, a.str, a.len);
    memcpy(res.str + a.len, b.str, b.len);
    res.len = a.len + b.len;
    return res;
}

buffer_t buf_repeat(buffer_t a, size_t reps) {
    size_t i;
    buffer_t res;
    res.str = malloc(sizeof(char)*a.len*reps);
    for(i=0; i<reps; i++) memcpy(res.str+i*a.len, a.str, a.len);
    res.len = a.len*reps;
    return res;
}

unsigned long buf_asulong(buffer_t buf) {
    unsigned long num=0;
    size_t i;
    for(i=0; i<buf.len; i++) {
        num = num * 10 + (buf.str[i] - '0');
    }
    return num;
}

void buf_free(buffer_t buf) {
    free(buf.str);
}

void tok_free(tok_t *tok) {
    if(tok->type = TOK_STR) buf_free(tok->buf);
}

typedef struct {
    tok_t cur;
    tok_t next;
    FILE *in;
} tokenizer_t;

int tokenizer_init(tokenizer_t *tz, FILE *in) {
    tz->in = in;
    if(next_tok(&(tz->cur), tz->in)) return 1;
    if(next_tok(&(tz->next), tz->in)) return 1;
    return 0;
}

void tokenizer_cleanup(tokenizer_t *tz) {
    tok_free(&(tz->cur));
    tok_free(&(tz->next));
}

tok_t tokenizer_peek(tokenizer_t *tz) {
    return tz->cur;
}

tok_t tokenizer_peeknext(tokenizer_t *tz) {
    return tz->next;
}

int tokenizer_consume(tokenizer_t *tz) {
    tok_free(&(tz->cur));
    tz->cur = tz->next;
    if(next_tok(&(tz->next), tz->in)) return 1;
    return 0;
}

typedef enum {
    EX_LIT,
    EX_CONCAT,
    EX_REPEAT
} nodetype_t;

typedef struct tag_astnode_t {
    nodetype_t type;
    union {
        buffer_t buf;
        struct {
            struct tag_astnode_t *left;
            struct tag_astnode_t *right;
        };
    };
} ast_node;

ast_node *ast_new_lit(buffer_t buf) {
    ast_node *res = malloc(sizeof(ast_node));
    res->buf = buf_dup(buf);
    return res;
}

ast_node *ast_new_binop(nodetype_t type, ast_node *left, ast_node *right) {
    ast_node *res = malloc(sizeof(ast_node));
    res->type = type;
    res->left = left;
    res->right = right;
    return res;
}

void ast_free(ast_node *node) {
    switch(node->type) {
        case EX_LIT:
            buf_free(node->buf);
            break;
            
        case EX_CONCAT: case EX_REPEAT:
            ast_free(node->left);
            ast_free(node->right);
            break;
    }
    free(node);
}

void print_lev(int lev, char *fmt, ...) {
    va_list va;
    int i;
    for(i = 0; i < lev; i++) { putchar('|'); putchar(' '); putchar(' '); putchar(' '); }
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
}

void ast_print_inner(ast_node *node, int lev) {
    switch(node->type) {
        case EX_LIT:
            print_lev(lev, "String literal:");
            buf_print(node->buf);
            printf("\n");
            break;
            
        case EX_CONCAT: case EX_REPEAT:
            print_lev(lev, "Binop: %c\n", (node->type==EX_CONCAT?'.':'^'));
            print_lev(lev+1, "Left:\n");
            ast_print_inner(node->left, lev+2);
            print_lev(lev+1, "Right:\n");
            ast_print_inner(node->right, lev+2);
            break;
            
        default:
            print_lev(lev, "<Unknown node>\n");
            break;
    }
}

void ast_print(ast_node *node) {
    ast_print_inner(node, 0);
}

ast_node *parse(tokenizer_t *);

ast_node *parse_toplevel(tokenizer_t *tz) {
    tok_t cur = tokenizer_peek(tz);
    ast_node *node;
    switch(cur.type) {
        case TOK_STR:
            if(tokenizer_consume(tz)) fatal("Syntax error after literal");
            return ast_new_lit(cur.buf);
            break;
            
        case TOK_OP:
            if(cur.op == '(') {
                if(tokenizer_consume(tz)) fatal("Syntax error after open paren");
                node = parse(tz);
                cur = tokenizer_peek(tz);
                if(cur.type != TOK_OP || cur.op != ')') fatal("Syntax error: expected close paren");
                if(tokenizer_consume(tz)) fatal("Syntax error after close paren");
                return node;
            }
            break;
    }
    fatal("Syntax error: expected toplevel expression");
    return NULL;
}

ast_node *parse_repeat(tokenizer_t *tz) {
    ast_node *left = parse_toplevel(tz), *right;
    tok_t cur = tokenizer_peek(tz), next;
    switch(cur.type) {
        case TOK_OP:
            if(cur.op == '^') {
                if(tokenizer_consume(tz)) fatal("Syntax error after repeat operator");
                next = tokenizer_peeknext(tz);
                if(next.type == TOK_OP && next.op == '^') {
                    right = parse_repeat(tz);
                } else {
                    right = parse_toplevel(tz);
                }
                return ast_new_binop(EX_REPEAT, left, right);
            }
            break;
    }
    return left;
}

ast_node *parse_concat(tokenizer_t *tz) {
    ast_node *left = parse_repeat(tz), *right;
    tok_t cur = tokenizer_peek(tz), next;
    while(cur.type == TOK_OP && cur.op == '.') {
        if(tokenizer_consume(tz)) fatal("Syntax error after concat operator");
        right = parse_repeat(tz);
        left = ast_new_binop(EX_CONCAT, left, right);
        cur = tokenizer_peek(tz);
    }
    return left;
}
    

ast_node *parse(tokenizer_t *tz) {
    return parse_concat(tz);
}

buffer_t eval(ast_node *node) {
    buffer_t l, r, res;
    switch(node->type) {
        case EX_LIT:
            return buf_dup(node->buf);
            break;
            
        case EX_CONCAT: case EX_REPEAT:
            l = eval(node->left);
            r = eval(node->right);
            if(node->type == EX_CONCAT) {
                res = buf_concat(l, r);
            } else {
                res = buf_repeat(l, buf_asulong(r));
            }
            buf_free(l);
            buf_free(r);
            return res;
            break;
    }
    fatal("Attempt to evaluate unknown node");
    return buf_new("", 0);
}

int main() {
    tokenizer_t tz;
    ast_node *p;
    buffer_t buf;
    if(tokenizer_init(&tz, stdin)) fatal("Failed to initialize tokenizer");
    p = parse(&tz);
    ast_print(p);
    buf = eval(p);
    buf_print(buf); printf("\n");
    ast_free(p);
    tokenizer_cleanup(&tz);
    return 0;
}