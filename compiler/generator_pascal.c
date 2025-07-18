#include <assert.h>
#include <stdlib.h> /* for exit */
#include <string.h> /* for strlen */
#include <stdio.h> /* for fprintf etc */
#include "header.h"

#define BASE_UNIT   "SnowballProgram"
#define BASE_CLASS  "T" BASE_UNIT

/* prototypes */

static void generate(struct generator * g, struct node * p);
static void w(struct generator * g, const char * s);
static void writef(struct generator * g, const char * s, struct node * p);

static int new_label(struct generator * g) {
    return g->next_label++;
}

static struct str * vars_newname(struct generator * g) {
    struct str * output;
    g->var_number++;
    output = str_new();
    str_append_string(output, "v_");
    str_append_int(output, g->var_number);
    return output;
}

/* Write routines for items from the syntax tree */

static void write_varname(struct generator * g, struct name * p) {
    if (p->type != t_external) {
        /* Pascal identifiers are case-insensitive but Snowball identifiers
         * should be case-sensitive.  To address this, we encode the case of
         * the identifier.  For readability of the generated code, the
         * encoding tries to be minimally intrusive for common cases.
         *
         * After the letter which indicates the type and before the "_" we
         * encode the case pattern in the Snowball identifier using "U" for
         * an upper-case letter, "l" for a lower-case letter and nothing for
         * other characters.  Any trailing string of "l" is omitted (since
         * it's redundant and decreases readability).
         *
         * Identifiers without any upper-case encode most simply, e.g. I_foo2
         *
         * A capitalised identifier is also concise, e.g. IU_Foo2
         *
         * All-caps gives a string of Us, e.g. IUUUUUUUU_SHOUTING
         *
         * But any example can be handled, e.g. IUllU_Foo79_Bar
         *
         * We don't try to solve this problem for external identifiers - it
         * seems more helpful to leave those alone and encourage snowball
         * program authors to avoid naming externals which only differ by
         * case.
         */
        int len = SIZE(p->s);
        int lower_pending = 0;
        write_char(g, "SBIrxg"[p->type]);
        for (int i = 0; i != len; ++i) {
            int ch = p->s[i];
            if (ch >= 'a' && ch <= 'z') {
                ++lower_pending;
            } else if (ch >= 'A' && ch <= 'Z') {
                while (lower_pending) {
                    write_char(g, 'l');
                    --lower_pending;
                }
                write_char(g, 'U');
            }
        }

        write_char(g, '_');
    }
    write_s(g, p->s);
}

static void write_literal_string(struct generator * g, symbol * p) {
    write_char(g, '\'');
    for (int i = 0; i < SIZE(p); i++) {
        int ch = p[i];
        if (ch == '\'') {
            write_string(g, "''");
        } else if (32 <= ch && ch < 127) {
            write_char(g, ch);
        } else {
            write_char(g, '\'');
            write_char(g, '#');
            write_int (g, ch);
            write_char(g, '\'');
        }
    }
    write_char(g, '\'');
}

static void write_margin(struct generator * g) {
    for (int i = 0; i < g->margin; i++) write_string(g, "    ");
}

static void write_relop(struct generator * g, int relop) {
    switch (relop) {
        case c_eq: write_string(g, " = "); break;
        case c_ne: write_string(g, " <> "); break;
        case c_gt: write_string(g, " > "); break;
        case c_ge: write_string(g, " >= "); break;
        case c_lt: write_string(g, " < "); break;
        case c_le: write_string(g, " <= "); break;
        default:
            fprintf(stderr, "Unexpected type #%d in generate_integer_test\n", relop);
            exit(1);
    }
}

/* Write a variable declaration. */
static void write_declare(struct generator * g,
                          const char * declaration,
                          struct node * p) {
    struct str * temp = g->outbuf;
    g->outbuf = g->declarations;
    write_string(g, "    ");
    writef(g, declaration, p);
    write_string(g, ";");
    write_newline(g);
    g->outbuf = temp;
}

static void write_comment(struct generator * g, struct node * p) {
    if (!g->options->comments) return;
    write_margin(g);
    write_string(g, "{ ");
    write_comment_content(g, p);
    write_string(g, " }");
    write_newline(g);
}

static void write_block_start(struct generator * g) {
    w(g, "~MBegin~+~N");
}

static void write_block_end(struct generator * g) {
    w(g, "~-~MEnd;~N");
}

static void write_savecursor(struct generator * g, struct node * p,
                             struct str * savevar) {
    g->B[0] = str_data(savevar);
    g->S[1] = "";
    if (p->mode != m_forward) g->S[1] = "FLimit - ";
    write_declare(g, "~B0 : Integer", p);
    writef(g, "~M~B0 := ~S1FCursor;~N" , p);
}

static void append_restore_string(struct node * p, struct str * out, struct str * savevar) {
    str_append_string(out, "FCursor := ");
    if (p->mode != m_forward) str_append_string(out, "FLimit - ");
    str_append(out, savevar);
    str_append_string(out, ";");
}

static void write_restorecursor(struct generator * g, struct node * p, struct str * savevar) {
    write_margin(g);
    append_restore_string(p, g->outbuf, savevar);
    write_newline(g);
}

static void write_inc_cursor(struct generator * g, struct node * p) {
    write_margin(g);
    write_string(g, p->mode == m_forward ? "Inc(FCursor);" : "Dec(FCursor);");
    write_newline(g);
}

static void wsetl(struct generator * g, int n) {
    w(g, "lab");
    write_int(g, n);
    w(g, ":~N");
}

static void write_failure(struct generator * g) {
    if (str_len(g->failure_str) != 0) {
        write_margin(g);
        write_str(g, g->failure_str);
        write_newline(g);
    }
    write_margin(g);
    switch (g->failure_label) {
        case x_return:
            write_string(g, "Begin Result := False; Exit; End;");
            g->unreachable = true;
            break;
        default:
            write_string(g, "goto lab");
            write_int(g, g->failure_label);
            write_string(g, ";");
            g->label_used = 1;
            g->unreachable = true;
    }
    write_newline(g);
}

static void write_failure_if(struct generator * g, const char * s, struct node * p) {
    writef(g, "~MIf (", p);
    writef(g, s, p);
    writef(g, ") Then~N", p);
    write_block_start(g);
    write_failure(g);
    write_block_end(g);
    g->unreachable = false;
}

/* if at limit fail */
static void write_check_limit(struct generator * g, struct node * p) {
    if (p->mode == m_forward) {
        write_failure_if(g, "FCursor >= FLimit", p);
    } else {
        write_failure_if(g, "FCursor <= FBkLimit", p);
    }
}

/* Formatted write. */
static void writef(struct generator * g, const char * input, struct node * p) {
    (void)p;
    int i = 0;

    while (input[i]) {
        int ch = input[i++];
        if (ch != '~') {
            write_char(g, ch);
            continue;
        }
        ch = input[i++];
        switch (ch) {
            case '~': write_char(g, '~'); continue;
            case 'f': write_block_start(g);
                      write_failure(g);
                      g->unreachable = false;
                      write_block_end(g);
                      continue;
            case 'M': write_margin(g); continue;
            case 'N': write_newline(g); continue;
            case '{': write_block_start(g); continue;
            case '}': write_block_end(g); continue;
            case 'S': {
                int j = input[i++] - '0';
                if (j < 0 || j > (int)(sizeof(g->S) / sizeof(g->S[0]))) {
                    printf("Invalid escape sequence ~%c%c in writef(g, \"%s\", p)\n",
                           ch, input[i - 1], input);
                    exit(1);
                }
                write_string(g, g->S[j]);
                continue;
            }
            case 'B': {
                int j = input[i++] - '0';
                if (j < 0 || j > (int)(sizeof(g->B) / sizeof(g->B[0])))
                    goto invalid_escape2;
                write_s(g, g->B[j]);
                continue;
            }
            case 'I': {
                int j = input[i++] - '0';
                if (j < 0 || j > (int)(sizeof(g->I) / sizeof(g->I[0])))
                    goto invalid_escape2;
                write_int(g, g->I[j]);
                continue;
            }
            case 'V':
            case 'W':
                write_varname(g, p->name);
                continue;
            case 'L':
                write_literal_string(g, p->literalstring);
                continue;
            case 's':
                write_int(g, SIZE(p->literalstring));
                continue;
            case '+': g->margin++; continue;
            case '-': g->margin--; continue;
            case 'n': write_string(g, g->options->name); continue;
            default:
                printf("Invalid escape sequence ~%c in writef(g, \"%s\", p)\n",
                       ch, input);
                exit(1);
            invalid_escape2:
                printf("Invalid escape sequence ~%c%c in writef(g, \"%s\", p)\n",
                       ch, input[i - 1], input);
                exit(1);
        }
    }
}

static void w(struct generator * g, const char * s) {
    writef(g, s, NULL);
}

static void generate_AE(struct generator * g, struct node * p) {
    const char * s;
    switch (p->type) {
        case c_name:
            write_varname(g, p->name); break;
        case c_number:
            write_int(g, p->number); break;
        case c_maxint:
            write_string(g, "MAXINT"); break;
        case c_minint:
            write_string(g, "(-MAXINT - 1)"); break;
        case c_neg:
            write_char(g, '-'); generate_AE(g, p->right); break;
        case c_multiply:
            s = " * "; goto label0;
        case c_plus:
            s = " + "; goto label0;
        case c_minus:
            s = " - "; goto label0;
        case c_divide:
            s = " div ";
        label0:
            write_char(g, '('); generate_AE(g, p->left);
            write_string(g, s); generate_AE(g, p->right); write_char(g, ')'); break;
        case c_cursor:
            w(g, "FCursor"); break;
        case c_limit:
            w(g, p->mode == m_forward ? "FLimit" : "FBkLimit"); break;
        case c_len:
        case c_size:
            w(g, "Length(current)");
            break;
        case c_lenof:
        case c_sizeof:
            writef(g, "Length(~V)", p);
            break;
    }
}

static void generate_bra(struct generator * g, struct node * p) {
    write_comment(g, p);
    p = p->left;
    while (p) {
        generate(g, p);
        p = p->right;
    }
}

static void generate_and(struct generator * g, struct node * p) {
    struct str * savevar = NULL;
    if (K_needed(g, p->left)) {
        savevar = vars_newname(g);
    }

    write_comment(g, p);

    if (savevar) write_savecursor(g, p, savevar);

    p = p->left;
    while (p) {
        generate(g, p);
        if (g->unreachable) break;
        if (savevar && p->right != NULL) write_restorecursor(g, p, savevar);
        p = p->right;
    }

    if (savevar) {
        str_delete(savevar);
    }
}

static void generate_or(struct generator * g, struct node * p) {
    struct str * savevar = NULL;
    if (K_needed(g, p->left)) {
        savevar = vars_newname(g);
    }

    int used = g->label_used;
    int a0 = g->failure_label;
    struct str * a1 = str_copy(g->failure_str);

    int end_unreachable = true;

    write_comment(g, p);
    w(g, "~MRepeat~N~+");

    if (savevar) write_savecursor(g, p, savevar);

    p = p->left;
    str_clear(g->failure_str);

    if (p == NULL) {
        /* p should never be NULL after an or: there should be at least two
         * sub nodes. */
        fprintf(stderr, "Error: \"or\" node without children nodes.");
        exit(1);
    }
    while (p->right) {
        g->failure_label = new_label(g);
        g->label_used = 0;
        generate(g, p);
        if (!g->unreachable) {
            w(g, "~MBreak;~N");
            end_unreachable = false;
        }

        if (g->label_used)
            wsetl(g, g->failure_label);
        g->unreachable = false;
        if (savevar) write_restorecursor(g, p, savevar);
        p = p->right;
    }

    g->label_used = used;
    g->failure_label = a0;
    str_delete(g->failure_str);
    g->failure_str = a1;

    generate(g, p);

    w(g, "~MUntil True;~N");
    if (!end_unreachable) {
        g->unreachable = false;
    }

    if (savevar) {
        str_delete(savevar);
    }
}

static void generate_backwards(struct generator * g, struct node * p) {
    write_comment(g, p);
    writef(g,"~MFBkLimit := FCursor; FCursor := FLimit;~N", p);
    generate(g, p->left);
    w(g, "~MFCursor := FBkLimit;~N");
}


static void generate_not(struct generator * g, struct node * p) {
    struct str * savevar = NULL;
    if (K_needed(g, p->left)) {
        savevar = vars_newname(g);
    }

    int a0 = g->failure_label;
    struct str * a1 = str_copy(g->failure_str);

    write_comment(g, p);
    if (savevar) {
        write_block_start(g);
        write_savecursor(g, p, savevar);
    }

    g->failure_label = new_label(g);
    str_clear(g->failure_str);

    int l = g->failure_label;

    generate(g, p->left);

    g->failure_label = a0;
    str_delete(g->failure_str);
    g->failure_str = a1;

    if (!g->unreachable) write_failure(g);

    if (g->label_used)
        wsetl(g, l);

    g->unreachable = false;

    if (savevar) {
        write_restorecursor(g, p, savevar);
        write_block_end(g);
        str_delete(savevar);
    }
}


static void generate_try(struct generator * g, struct node * p) {
    struct str * savevar = NULL;
    if (K_needed(g, p->left)) {
        savevar = vars_newname(g);
    }

    g->failure_label = new_label(g);
    g->label_used = 0;
    str_clear(g->failure_str);

    write_comment(g, p);
    if (savevar) {
        write_savecursor(g, p, savevar);
        append_restore_string(p, g->failure_str, savevar);
    }

    generate(g, p->left);
    if (g->label_used)
        wsetl(g, g->failure_label);
    g->unreachable = false;

    if (savevar) {
        str_delete(savevar);
    }
}

static void generate_set(struct generator * g, struct node * p) {
    write_comment(g, p);
    writef(g, "~M~V := True;~N", p);
}

static void generate_unset(struct generator * g, struct node * p) {
    write_comment(g, p);
    writef(g, "~M~V := False;~N", p);
}

static void generate_fail(struct generator * g, struct node * p) {
    write_comment(g, p);
    generate(g, p->left);
    if (!g->unreachable) write_failure(g);
}

/* generate_test() also implements 'reverse' */

static void generate_test(struct generator * g, struct node * p) {
    struct str * savevar = NULL;
    if (K_needed(g, p->left)) {
        savevar = vars_newname(g);
    }

    write_comment(g, p);

    if (savevar) {
        write_savecursor(g, p, savevar);
    }

    generate(g, p->left);

    if (savevar) {
        if (!g->unreachable) {
            write_restorecursor(g, p, savevar);
        }
        str_delete(savevar);
    }
}

static void generate_do(struct generator * g, struct node * p) {
    struct str * savevar = NULL;
    if (K_needed(g, p->left)) {
        savevar = vars_newname(g);
    }

    write_comment(g, p);
    if (savevar) write_savecursor(g, p, savevar);

    if (p->left->type == c_call) {
        /* Optimise do <call> */
        write_comment(g, p->left);
        writef(g, "~M~V();~N", p->left);
    } else {
        g->failure_label = new_label(g);
        str_clear(g->failure_str);

        generate(g, p->left);
        if (g->label_used)
            wsetl(g, g->failure_label);
        g->unreachable = false;
    }

    if (savevar) {
        write_restorecursor(g, p, savevar);
        str_delete(savevar);
    }
}

static void generate_next(struct generator * g, struct node * p) {
    write_comment(g, p);
    write_check_limit(g, p);
    write_inc_cursor(g, p);
}

static void generate_GO_grouping(struct generator * g, struct node * p, int is_goto, int complement) {
    write_comment(g, p);

    struct grouping * q = p->name->grouping;
    g->S[0] = p->mode == m_forward ? "" : "Bk";
    g->S[1] = complement ? "In" : "Out";
    g->I[0] = q->smallest_ch;
    g->I[1] = q->largest_ch;
    write_failure_if(g, "Not (Go~S1Grouping~S0(~V, ~I0, ~I1))", p);
    if (!is_goto) {
        w(g, p->mode == m_forward ? "~MInc(FCursor);~N" : "~MDec(FCursor);~N");
    }
}

static void generate_GO(struct generator * g, struct node * p, int style) {
    write_comment(g, p);

    int used = g->label_used;

    int a0 = g->failure_label;
    struct str * a1 = str_copy(g->failure_str);

    int end_unreachable = false;

    int golab = new_label(g);

    w(g, "~MWhile True Do~N");
    w(g, "~{");

    struct str * savevar = NULL;
    if (style == 1 || repeat_restore(g, p->left)) {
        savevar = vars_newname(g);
        write_savecursor(g, p, savevar);
    }

    g->failure_label = new_label(g);
    g->label_used = 0;
    str_clear(g->failure_str);
    generate(g, p->left);

    if (g->unreachable) {
        /* Cannot break out of this loop: therefore the code after the
         * end of the loop is unreachable.*/
        end_unreachable = true;
    } else {
        /* include for goto; omit for gopast */
        if (style == 1) write_restorecursor(g, p, savevar);
        g->I[0] = golab;
        w(g, "~Mgoto lab~I0;~N");
    }
    g->unreachable = false;
    if (g->label_used)
        wsetl(g, g->failure_label);
    if (savevar) {
        write_restorecursor(g, p, savevar);
        str_delete(savevar);
    }
    g->label_used = used;
    g->failure_label = a0;
    str_delete(g->failure_str);
    g->failure_str = a1;

    write_check_limit(g, p);
    write_inc_cursor(g, p);

    g->I[0] = golab;
    w(g, "~}lab~I0:~N");
    g->unreachable = end_unreachable;
}

static void generate_loop(struct generator * g, struct node * p) {
    struct str * loopvar = vars_newname(g);
    write_comment(g, p);
    g->B[0] = str_data(loopvar);
    write_declare(g, "~B0 : Integer", p);
    w(g, "~MFor ~B0 := ");
    generate_AE(g, p->AE);
    writef(g, " DownTo 1 Do~N", p);
    writef(g, "~{", p);

    generate(g, p->left);

    w(g, "~}");
    str_delete(loopvar);
    g->unreachable = false;
}

static void generate_repeat_or_atleast(struct generator * g, struct node * p, struct str * loopvar) {
    int replab = new_label(g);
    g->I[0] = replab;
    writef(g, "lab~I0:~N~MWhile True Do~N~{", p);

    struct str * savevar = NULL;
    if (repeat_restore(g, p->left)) {
        savevar = vars_newname(g);
        write_savecursor(g, p, savevar);
    }

    g->failure_label = new_label(g);
    str_clear(g->failure_str);
    generate(g, p->left);

    if (!g->unreachable) {
        if (loopvar != NULL) {
            g->B[0] = str_data(loopvar);
            w(g, "~MDec(~B0);~N");
        }

        g->I[0] = replab;
        w(g, "~Mgoto lab~I0;~N");
    }
    if (g->label_used)
        wsetl(g, g->failure_label);
    g->unreachable = false;

    if (savevar) {
        write_restorecursor(g, p, savevar);
        str_delete(savevar);
    }

    w(g, "~MBreak;~N~}");
}

static void generate_repeat(struct generator * g, struct node * p) {
    write_comment(g, p);
    generate_repeat_or_atleast(g, p, NULL);
}

static void generate_atleast(struct generator * g, struct node * p) {
    struct str * loopvar = vars_newname(g);

    write_comment(g, p);
    w(g, "~{");
    g->B[0] = str_data(loopvar);
    write_declare(g, "~B0 : Integer", p);
    w(g, "~M~B0 := ");
    generate_AE(g, p->AE);
    w(g, ";~N");
    {
        int a0 = g->failure_label;
        struct str * a1 = str_copy(g->failure_str);

        generate_repeat_or_atleast(g, p, loopvar);

        g->failure_label = a0;
        str_delete(g->failure_str);
        g->failure_str = a1;
    }
    g->B[0] = str_data(loopvar);
    write_failure_if(g, "~B0 > 0", p);
    w(g, "~}");
    str_delete(loopvar);
}

static void generate_setmark(struct generator * g, struct node * p) {
    write_comment(g, p);
    writef(g, "~M~W := FCursor;~N", p);
}

static void generate_tomark(struct generator * g, struct node * p) {
    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? ">" : "<";

    w(g, "~MIf (FCursor ~S0 "); generate_AE(g, p->AE); w(g, ") Then~N");
    write_block_start(g);
    write_failure(g);
    write_block_end(g);
    g->unreachable = false;
    w(g, "~MFCursor := "); generate_AE(g, p->AE); writef(g, ";~N", p);
}

static void generate_atmark(struct generator * g, struct node * p) {
    write_comment(g, p);
    w(g, "~MIf (FCursor <> "); generate_AE(g, p->AE); writef(g, ") Then~N", p);
    write_block_start(g);
    write_failure(g);
    write_block_end(g);
    g->unreachable = false;
}

static void generate_hop(struct generator * g, struct node * p) {
    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "+" : "-";

    w(g, "~{~MC := FCursor ~S0 ");
    generate_AE(g, p->AE);
    w(g, ";~N");

    g->S[1] = p->mode == m_forward ? "> FLimit" : "< FBkLimit";
    g->S[2] = p->mode == m_forward ? "<" : ">";
    if (p->AE->type == c_number) {
        // Constant distance hop.
        //
        // No need to check for negative hop as that's converted to false by
        // the analyser.
        write_failure_if(g, "C ~S1", p);
    } else {
        write_failure_if(g, "(C ~S1) Or (C ~S2 FCursor)", p);
    }
    writef(g, "~MFCursor := C;~N", p);
    writef(g, "~}", p);
    g->temporary_used = true;
}

static void generate_delete(struct generator * g, struct node * p) {
    write_comment(g, p);
    writef(g, "~MSliceDel;~N", p);
}

static void generate_tolimit(struct generator * g, struct node * p) {
    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "FLimit" : "FBkLimit";
    writef(g, "~MFCursor := ~S0;~N", p);
}

static void generate_atlimit(struct generator * g, struct node * p) {
    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "FLimit" : "FBkLimit";
    g->S[1] = p->mode == m_forward ? "<" : ">";
    write_failure_if(g, "FCursor ~S1 ~S0", p);
}

static void generate_leftslice(struct generator * g, struct node * p) {
    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "FBra" : "FKet";
    writef(g, "~M~S0 := FCursor;~N", p);
}

static void generate_rightslice(struct generator * g, struct node * p) {
    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "FKet" : "FBra";
    writef(g, "~M~S0 := FCursor;~N", p);
}

static void generate_assignto(struct generator * g, struct node * p) {
    write_comment(g, p);
    writef(g, "~M~V := AssignTo();~N", p);
}

static void generate_sliceto(struct generator * g, struct node * p) {
    write_comment(g, p);
    writef(g, "~M~V := SliceTo();~N", p);
}

static void generate_address(struct generator * g, struct node * p) {
    symbol * b = p->literalstring;
    if (b != NULL) {
        write_literal_string(g, b);
    } else {
        write_varname(g, p->name);
    }
}

static void generate_insert(struct generator * g, struct node * p, int style) {
    int keep_c = style == c_attach;
    write_comment(g, p);
    if (p->mode == m_backward) keep_c = !keep_c;
    if (keep_c) {
        w(g, "~{~MC := FCursor;~N");
        g->temporary_used = true;
    }
    writef(g, "~Minsert(FCursor, FCursor, ", p);
    generate_address(g, p);
    writef(g, ");~N", p);
    if (keep_c) w(g, "~MFCursor := C;~N~}");
}

static void generate_assignfrom(struct generator * g, struct node * p) {
    int keep_c = p->mode == m_forward; /* like 'attach' */

    write_comment(g, p);
    if (keep_c) {
        writef(g, "~{~MC := FCursor;~N", p);
        g->temporary_used = true;
    }
    if (p->mode == m_forward) {
        writef(g, "~Minsert(FCursor, FLimit, ", p);
    } else {
        writef(g, "~Minsert(FBkLimit, FCursor, ", p);
    }
    generate_address(g, p);
    writef(g, ");~N", p);
    if (keep_c) w(g, "~MFCursor := c;~N~}");
}

static void generate_slicefrom(struct generator * g, struct node * p) {
    write_comment(g, p);
    w(g, "~MSliceFrom(");
    generate_address(g, p);
    writef(g, ");~N", p);
}

static void generate_setlimit(struct generator * g, struct node * p) {
    struct str * varname = vars_newname(g);
    write_comment(g, p);
    if (p->left && p->left->type == c_tomark) {
        /* Special case for:
         *
         *   setlimit tomark AE for C
         *
         * All uses of setlimit in the current stemmers we ship follow this
         * pattern, and by special-casing we can avoid having to save and
         * restore c.
         */
        struct node * q = p->left;
        write_comment(g, q);
        g->S[0] = q->mode == m_forward ? ">" : "<";
        w(g, "~MIf (FCursor ~S0 "); generate_AE(g, q->AE); w(g, ") Then~N");
        write_block_start(g);
        write_failure(g);
        write_block_end(g);
        g->unreachable = false;

        g->B[0] = str_data(varname);
        write_declare(g, "~B0 : Integer", p);
        if (p->mode == m_forward) {
            w(g, "~M~B0 := FLimit - FCursor;~N");
            w(g, "~MFLimit := ");
        } else {
            w(g, "~M~B0 := FBkLimit;~N");
            w(g, "~MFBkLimit := ");
        }
        generate_AE(g, q->AE);
        writef(g, ";~N", q);

        if (p->mode == m_forward) {
            str_assign(g->failure_str, "FLimit := FLimit + ");
            str_append(g->failure_str, varname);
            str_append_ch(g->failure_str, ';');
        } else {
            str_assign(g->failure_str, "FBkLimit := ");
            str_append(g->failure_str, varname);
            str_append_ch(g->failure_str, ';');
        }
    } else {
        struct str * savevar = vars_newname(g);
        write_savecursor(g, p, savevar);

        generate(g, p->left);

        if (!g->unreachable) {
            g->B[0] = str_data(varname);
            write_declare(g, "~B0 : Integer", p);
            if (p->mode == m_forward) {
                w(g, "~M~B0 := FLimit - FCursor;~N");
                w(g, "~MFLimit := FCursor;~N");
            } else {
                w(g, "~M~B0 := FBkLimit;~N");
                w(g, "~MFBkLimit := FCursor;~N");
            }
            write_restorecursor(g, p, savevar);

            if (p->mode == m_forward) {
                str_assign(g->failure_str, "FLimit := FLimit + ");
                str_append(g->failure_str, varname);
                str_append_ch(g->failure_str, ';');
            } else {
                str_assign(g->failure_str, "FBkLimit := ");
                str_append(g->failure_str, varname);
                str_append_ch(g->failure_str, ';');
            }
        }
        str_delete(savevar);
    }

    if (!g->unreachable) {
        generate(g, p->aux);

        if (!g->unreachable) {
            write_margin(g);
            write_str(g, g->failure_str);
            write_newline(g);
        }
    }
    str_delete(varname);
}

/* dollar sets snowball up to operate on a string variable as if it were the
 * current string */
static void generate_dollar(struct generator * g, struct node * p) {
    write_comment(g, p);

    struct str * savevar = vars_newname(g);
    g->B[0] = str_data(savevar);

    {
        struct str * saved_output = g->outbuf;
        str_clear(g->failure_str);
        g->outbuf = g->failure_str;
        writef(g, "~V := FCurrent; "
                  "FCurrent := ~B0_Current; "
                  "FCursor := ~B0_Cursor; "
                  "FLimit := ~B0_Limit; "
                  "FBkLimit := ~B0_BkLimit; "
                  "FBra := ~B0_Bra; "
                  "FKet := ~B0_Ket;", p);
        g->failure_str = g->outbuf;
        g->outbuf = saved_output;
    }

    write_declare(g, "~B0_Current : AnsiString", p);
    write_declare(g, "~B0_Cursor : Integer", p);
    write_declare(g, "~B0_Limit : Integer", p);
    write_declare(g, "~B0_BkLimit : Integer", p);
    write_declare(g, "~B0_Bra : Integer", p);
    write_declare(g, "~B0_Ket : Integer", p);
    writef(g, "~{"
              "~M~B0_Current := FCurrent;~N"
              "~M~B0_Cursor := FCursor;~N"
              "~M~B0_Limit := FLimit;~N"
              "~M~B0_BkLimit := FBkLimit;~N"
              "~M~B0_Bra := FBra;~N"
              "~M~B0_Ket := FKet;~N"
              "~MFCurrent := ~V;~N"
              "~MFCursor := 0;~N"
              "~MFLimit := Length(current);~N", p);
    generate(g, p->left);
    if (!g->unreachable) {
        write_margin(g);
        write_str(g, g->failure_str);
        write_newline(g);
    }
    w(g, "~}");
    str_delete(savevar);
}

static void generate_integer_assign(struct generator * g, struct node * p, const char * s) {
    write_comment(g, p);
    writef(g, "~M~W := ", p);

    if (s != NULL) {
        g->S[0] = s;
        writef(g, "~W ~S0 ", p);
    }

    generate_AE(g, p->AE);
    w(g, ";~N");
}

static void generate_integer_test(struct generator * g, struct node * p) {
    write_comment(g, p);
    int relop = p->type;
    int optimise_to_return = (g->failure_label == x_return && p->right && p->right->type == c_functionend);
    if (optimise_to_return) {
        w(g, "~MResult := ");
        p->right = NULL;
    } else {
        w(g, "~MIf ");
        // We want the inverse of the snowball test here.
        relop ^= 1;
    }
    generate_AE(g, p->left);
    write_relop(g, relop);
    generate_AE(g, p->AE);
    if (optimise_to_return) {
        w(g, "~N");
    } else {
        w(g, " Then~N");
        write_block_start(g);
        write_failure(g);
        write_block_end(g);
        g->unreachable = false;
    }
}

static void generate_call(struct generator * g, struct node * p) {
    int signals = p->name->definition->possible_signals;
    write_comment(g, p);
    if (g->failure_label == x_return) {
        if (p->right && p->right->type == c_functionend) {
            /* Tail call. */
            writef(g, "~MResult := ~V;~N", p);
            p->right = NULL;
            return;
        }
        if (signals == 0) {
            /* Always fails. */
            writef(g, "~MBegin; Result := ~V; Exit; End;~N", p);
            g->unreachable = true;
            return;
        }
    }
    if (signals == 1) {
        /* Always succeeds. */
        writef(g, "~M~V;~N", p);
    } else if (signals == 0) {
        /* Always fails. */
        writef(g, "~M~V;~N", p);
        write_failure(g);
    } else {
        write_failure_if(g, "Not ~V", p);
    }
}

static void generate_grouping(struct generator * g, struct node * p, int complement) {
    write_comment(g, p);

    struct grouping * q = p->name->grouping;
    g->S[0] = p->mode == m_forward ? "" : "Bk";
    g->S[1] = complement ? "Out" : "In";
    g->I[0] = q->smallest_ch;
    g->I[1] = q->largest_ch;
    write_failure_if(g, "Not (~S1Grouping~S0(~V, ~I0, ~I1))", p);
}

static void generate_namedstring(struct generator * g, struct node * p) {
    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "" : "Bk";
    write_failure_if(g, "Not (EqS~S0(~V))", p);
}

static void generate_literalstring(struct generator * g, struct node * p) {
    write_comment(g, p);
    g->S[0] = p->mode == m_forward ? "" : "Bk";
    write_failure_if(g, "Not (EqS~S0(~L))", p);
}

static void generate_define(struct generator * g, struct node * p) {
    struct name * q = p->name;
    if (q->type == t_routine && !q->used) return;

    write_newline(g);
    write_comment(g, p);

    /* Generate function header. */
    writef(g, "~MFunction T~n.~W : Boolean;~N", p);

    /* Save output. */
    struct str *saved_output = g->outbuf;
    struct str *saved_declarations = g->declarations;
    g->outbuf = str_new();
    g->declarations = str_new();

    g->next_label = 0;
    g->var_number = 0;

    str_clear(g->failure_str);
    g->failure_label = x_return;
    g->unreachable = false;

    /* Generate function body. */
    w(g, "~{");
    int signals = p->left->possible_signals;
    g->temporary_used = false;
    generate(g, p->left);
    if (p->left->right) {
        assert(p->left->right->type == c_functionend);
        if (signals) {
            generate(g, p->left->right);
        }
    }
    w(g, "~}");

    if (g->temporary_used) {
        str_append_string(g->declarations, "    C : Integer;\n");
    }

    if (q->amongvar_needed) {
        str_append_string(g->declarations, "    AmongVar : Integer;\n");
    }

    if (str_len(g->declarations) > 0) {
        str_append_string(saved_output, "Var\n");
        str_append(saved_output, g->declarations);
    }

    if (g->next_label) {
        str_append_string(saved_output, "Label\n");

        int num = g->next_label;
        for (int i = 0; i < num; ++i) {
            str_append_string(saved_output, "    lab");
            str_append_int(saved_output, i);
            str_append_string(saved_output, i == num - 1 ? ";\n" : ",\n");
        }
    }

    str_append(saved_output, g->outbuf);
    str_delete(g->declarations);
    str_delete(g->outbuf);
    g->declarations = saved_declarations;
    g->outbuf = saved_output;
}

static void generate_functionend(struct generator * g, struct node * p) {
    (void)p;
    w(g, "~MResult := True;~N");
}

static void generate_substring(struct generator * g, struct node * p) {
    write_comment(g, p);

    struct among * x = p->among;

    g->S[0] = p->mode == m_forward ? "" : "Bk";
    g->I[0] = x->number;
    g->I[1] = x->literalstring_count;

    if (x->amongvar_needed) {
        writef(g, "~MAmongVar := FindAmong~S0(a_~I0, ~I1);~N", p);
        if (!x->always_matches) {
            write_failure_if(g, "AmongVar = 0", p);
        }
    } else if (x->always_matches) {
        writef(g, "~MFindAmong~S0(a_~I0, ~I1);~N", p);
    } else if (x->command_count == 0 &&
               g->failure_label == x_return &&
               x->node->right && x->node->right->type == c_functionend) {
        writef(g, "~MResult := FindAmong~S0(a_~I0, ~I1) <> 0;~N", p);
        x->node->right = NULL;
    } else {
        write_failure_if(g, "FindAmong~S0(a_~I0, ~I1) = 0", p);
    }
}

static void generate_among(struct generator * g, struct node * p) {
    struct among * x = p->among;

    if (x->substring == NULL) {
        generate_substring(g, p);
    } else {
        write_comment(g, p);
    }

    if (x->command_count == 1 && x->nocommand_count == 0) {
        /* Only one outcome ("no match" already handled). */
        generate(g, x->commands[0]);
    } else if (x->command_count > 0) {
        w(g, "~MCase AmongVar Of~N~+");
        for (int i = 1; i <= x->command_count; i++) {
            g->I[0] = i;
            w(g, "~M~I0:~N~{");
            generate(g, x->commands[i - 1]);
            w(g, "~}");
            g->unreachable = false;
        }
        write_block_end(g);
    }
}

static void generate_booltest(struct generator * g, struct node * p, int inverted) {
    write_comment(g, p);
    if (g->failure_label == x_return) {
        if (p->right && p->right->type == c_functionend) {
            // Optimise at end of function.
            if (inverted) {
                writef(g, "~MResult := !~V;~N", p);
            } else {
                writef(g, "~MResult := ~V;~N", p);
            }
            p->right = NULL;
            return;
        }
    }
    if (inverted) {
        write_failure_if(g, "~V", p);
    } else {
        write_failure_if(g, "Not (~V)", p);
    }
}

static void generate_false(struct generator * g, struct node * p) {
    write_comment(g, p);
    write_failure(g);
}

static void generate_debug(struct generator * g, struct node * p) {
    write_comment(g, p);
    g->I[0] = g->debug_count++;
    g->I[1] = p->line_number;
    writef(g, "~Mdebug(~I0, ~I1);~N", p);
}

static void generate(struct generator * g, struct node * p) {
    if (g->unreachable) return;

    int a0 = g->failure_label;
    struct str * a1 = str_copy(g->failure_str);

    switch (p->type) {
        case c_define:        generate_define(g, p); break;
        case c_bra:           generate_bra(g, p); break;
        case c_and:           generate_and(g, p); break;
        case c_or:            generate_or(g, p); break;
        case c_backwards:     generate_backwards(g, p); break;
        case c_not:           generate_not(g, p); break;
        case c_set:           generate_set(g, p); break;
        case c_unset:         generate_unset(g, p); break;
        case c_try:           generate_try(g, p); break;
        case c_fail:          generate_fail(g, p); break;
        case c_reverse:
        case c_test:          generate_test(g, p); break;
        case c_do:            generate_do(g, p); break;
        case c_goto:          generate_GO(g, p, 1); break;
        case c_gopast:        generate_GO(g, p, 0); break;
        case c_goto_grouping: generate_GO_grouping(g, p, 1, 0); break;
        case c_gopast_grouping:
                              generate_GO_grouping(g, p, 0, 0); break;
        case c_goto_non:      generate_GO_grouping(g, p, 1, 1); break;
        case c_gopast_non:    generate_GO_grouping(g, p, 0, 1); break;
        case c_repeat:        generate_repeat(g, p); break;
        case c_loop:          generate_loop(g, p); break;
        case c_atleast:       generate_atleast(g, p); break;
        case c_setmark:       generate_setmark(g, p); break;
        case c_tomark:        generate_tomark(g, p); break;
        case c_atmark:        generate_atmark(g, p); break;
        case c_hop:           generate_hop(g, p); break;
        case c_delete:        generate_delete(g, p); break;
        case c_next:          generate_next(g, p); break;
        case c_tolimit:       generate_tolimit(g, p); break;
        case c_atlimit:       generate_atlimit(g, p); break;
        case c_leftslice:     generate_leftslice(g, p); break;
        case c_rightslice:    generate_rightslice(g, p); break;
        case c_assignto:      generate_assignto(g, p); break;
        case c_sliceto:       generate_sliceto(g, p); break;
        case c_assign:        generate_assignfrom(g, p); break;
        case c_insert:
        case c_attach:        generate_insert(g, p, p->type); break;
        case c_slicefrom:     generate_slicefrom(g, p); break;
        case c_setlimit:      generate_setlimit(g, p); break;
        case c_dollar:        generate_dollar(g, p); break;
        case c_mathassign:    generate_integer_assign(g, p, NULL); break;
        case c_plusassign:    generate_integer_assign(g, p, "+"); break;
        case c_minusassign:   generate_integer_assign(g, p, "-"); break;
        case c_multiplyassign:generate_integer_assign(g, p, "*"); break;
        case c_divideassign:  generate_integer_assign(g, p, "div"); break;
        case c_eq:
        case c_ne:
        case c_gt:
        case c_ge:
        case c_lt:
        case c_le:
            generate_integer_test(g, p);
            break;
        case c_call:          generate_call(g, p); break;
        case c_grouping:      generate_grouping(g, p, false); break;
        case c_non:           generate_grouping(g, p, true); break;
        case c_name:          generate_namedstring(g, p); break;
        case c_literalstring: generate_literalstring(g, p); break;
        case c_among:         generate_among(g, p); break;
        case c_substring:     generate_substring(g, p); break;
        case c_booltest:      generate_booltest(g, p, false); break;
        case c_not_booltest:  generate_booltest(g, p, true); break;
        case c_false:         generate_false(g, p); break;
        case c_true:          break;
        case c_debug:         generate_debug(g, p); break;
        case c_functionend:   generate_functionend(g, p); break;
        default: fprintf(stderr, "%d encountered\n", p->type);
                 exit(1);
    }

    g->failure_label = a0;
    str_delete(g->failure_str);
    g->failure_str = a1;
}

/* Class declaration generation. */
static void generate_unit_start(struct generator * g) {
    write_start_comment(g, "{ ", " }");
    w(g, "Unit ~n;~N~N{$HINTS OFF}~N~NInterface~N~NUses " BASE_UNIT ";~N");
}

static void generate_unit_end(struct generator * g) {
    w(g, "~NEnd.~N");
}

static void generate_class_begin(struct generator * g) {
    w(g, "~NType~N~+~MT~n = Class(" BASE_CLASS ")~N");
}

static void generate_class_end(struct generator * g) {
    w(g, "~}~NImplementation~N");
}

static void generate_method_decl(struct generator * g, struct name * q) {
    w(g, "~MFunction ");
    write_varname(g, q);
    w(g, " : Boolean;");
    if (q->type == t_external) {
        w(g, " Override;");
    }
    w(g, "~N");
}

static void generate_method_decls(struct generator * g) {
    w(g, "~Mpublic~N~+");
    w(g, "~MConstructor Create;~N");

    for (struct name * q = g->analyser->names; q; q = q->next) {
        if (q->type == t_external) {
            generate_method_decl(g, q);
        }
    }
    w(g, "~-");

    int first = true;
    for (struct name * q = g->analyser->names; q; q = q->next) {
        if (q->type == t_routine) {
            if (first) {
                w(g, "~Mprivate~N~+");
                first = false;
            }
            generate_method_decl(g, q);
        }
    }
    if (!first) w(g, "~-");
}

static void generate_member_decls(struct generator * g) {
    int first = true;
    for (struct name * q = g->analyser->names; q; q = q->next) {
        switch (q->type) {
            case t_string:
            case t_integer:
            case t_boolean:
                if (first) {
                    w(g, "~Mprivate~N~+");
                    first = false;
                }
                switch (q->type) {
                    case t_string:
                        write_margin(g);
                        write_varname(g, q);
                        w(g, " : AnsiString;~N");
                        break;
                    case t_integer:
                        write_margin(g);
                        write_varname(g, q);
                        w(g, " : Integer;~N");
                        break;
                    case t_boolean:
                        write_margin(g);
                        write_varname(g, q);
                        w(g, " : Boolean;~N");
                        break;
                }
        }
    }

    if (!first) w(g, "~-");
}

static void generate_among_decls(struct generator * g) {
    struct among *a = g->analyser->amongs;
    if (a == NULL) return;

    w(g, "~Mprivate~N~+");

    while (a != NULL) {
        g->I[0] = a->number;
        w(g, "~Ma_~I0 : Array Of TAmong;~N");
        a = a->next;
    }

    w(g, "~-");
}

static void generate_among_table(struct generator * g, struct among * x) {
    write_comment(g, x->node);

    struct amongvec * v = x->b;

    g->I[0] = x->number;
    g->I[1] = x->literalstring_count;

    w(g, "~MSetLength(a_~I0, ~I1);~N~+");
    for (int i = 0; i < x->literalstring_count; i++) {
        g->I[1] = i;

        /* Write among's string. */
        w(g, "~Ma_~I0[~I1].Str  := ");
        write_literal_string(g, v[i].b);
        w(g, ";~N");

        /* Write among's index & result. */
        g->I[2] = v[i].i;
        w(g, "~Ma_~I0[~I1].Index := ~I2;~N");
        g->I[2] = v[i].result;
        w(g, "~Ma_~I0[~I1].Result := ~I2;~N");

        /* Write among's handler. */
        w(g, "~Ma_~I0[~I1].Method := ");
        if (v[i].function == NULL) {
            w(g, "nil;~N~N");
        } else {
            w(g, "Self.");
            write_varname(g, v[i].function);
            w(g, ";~N~N");
        }
    }
    w(g, "~-");
}

static void generate_amongs(struct generator * g) {
    for (struct among * x = g->analyser->amongs; x; x = x->next) {
        generate_among_table(g, x);
    }
}

static void generate_constructor(struct generator * g) {
    w(g, "~N~MConstructor T~n.Create;~N~{");
    generate_amongs(g);
    w(g, "~}");
}

static void generate_methods(struct generator * g) {
    struct node * p = g->analyser->program;
    while (p != NULL) {
        generate(g, p);
        g->unreachable = false;
        p = p->right;
    }
}

static void set_bit(symbol * b, int i) { b[i/8] |= 1 << i%8; }

static void generate_grouping_table(struct generator * g, struct grouping * q) {
    int range = q->largest_ch - q->smallest_ch + 1;
    int size = (range + 7)/ 8;  /* assume 8 bits per symbol */
    symbol * b = q->b;
    symbol * map = create_b(size);

    for (int i = 0; i < size; i++) map[i] = 0;

    for (int i = 0; i < SIZE(b); i++) set_bit(map, b[i] - q->smallest_ch);

    g->I[0] = size - 1;
    w(g, "~N~MConst~+~N~M");
    write_varname(g, q->name);
    w(g, " : Array [0..~I0] Of Char = (~N~+");
    for (int i = 0; i < size; i++) {
        if (i != 0) w(g, ",~N");
        g->I[0] = map[i];
        w(g, "~MChr(~I0)");
    }
    w(g, "~N~-~M);~N~-");

    lose_b(map);
}

static void generate_groupings(struct generator * g) {
    for (struct grouping * q = g->analyser->groupings; q; q = q->next) {
        if (q->name->used)
            generate_grouping_table(g, q);
    }
}

extern void generate_program_pascal(struct generator * g) {
    g->outbuf = str_new();
    g->failure_str = str_new();

    generate_unit_start(g);

    /* Generate class declaration. */
    generate_class_begin(g);
    generate_member_decls(g);
    generate_among_decls(g);
    generate_method_decls(g);
    generate_class_end(g);

    /* generate implementation. */
    generate_groupings(g);
    generate_constructor(g);
    generate_methods(g);

    generate_unit_end(g);

    output_str(g->options->output_src, g->outbuf);
    str_delete(g->failure_str);
    str_delete(g->outbuf);
}
