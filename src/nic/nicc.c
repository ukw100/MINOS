/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nicc.c - nic compiler
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2017-2021 Frank Meyer - frank(at)fli4l.de
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <setjmp.h>
#include <fcntl.h>
#if defined (WIN32)
#include <windows.h>
#include <conio.h>
#elif defined (unix)
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#elif defined (STM32F4XX)
#include <unistd.h>
#else
#error unknown os
#endif

#include "nicstrings.h"
#include "nic-common.h"
#include "alloc.h"
#include "mcurses.h"
#include "nic-base.h"

#define DEFINE_FUNCTIONS                0
#include "funclist.h"

#if ALLOC_ENABLED == 1
#define ARG_UNUSED(x)   x
#else
#define ARG_UNUSED(x)   UNUSED_ ## x __attribute__((__unused__))
#endif

enum
{
    EXPRESSION_CONTENT_TYPE_INT_CONSTANT,
    EXPRESSION_CONTENT_TYPE_STRING_CONSTANT,
    EXPRESSION_CONTENT_TYPE_LOCAL_INT_VARIABLE,
    EXPRESSION_CONTENT_TYPE_LOCAL_INT_ARRAY_VARIABLE,
    EXPRESSION_CONTENT_TYPE_LOCAL_BYTE_VARIABLE,
    EXPRESSION_CONTENT_TYPE_LOCAL_BYTE_ARRAY_VARIABLE,
    EXPRESSION_CONTENT_TYPE_LOCAL_STRING_VARIABLE,
    EXPRESSION_CONTENT_TYPE_LOCAL_STRING_ARRAY_VARIABLE,
    EXPRESSION_CONTENT_TYPE_GLOBAL_INT_VARIABLE,
    EXPRESSION_CONTENT_TYPE_GLOBAL_INT_ARRAY_VARIABLE,
    EXPRESSION_CONTENT_TYPE_GLOBAL_BYTE_VARIABLE,
    EXPRESSION_CONTENT_TYPE_GLOBAL_BYTE_ARRAY_VARIABLE,
    EXPRESSION_CONTENT_TYPE_GLOBAL_STRING_VARIABLE,
    EXPRESSION_CONTENT_TYPE_GLOBAL_STRING_ARRAY_VARIABLE,
    EXPRESSION_CONTENT_TYPE_INTERN_FUNCTION,
    EXPRESSION_CONTENT_TYPE_EXTERN_FUNCTION,
    EXPRESSION_CONTENT_TYPE_UNDEFINED_FUNCTION,
};

typedef struct
{
    uint8_t            type;                                    // expression type
    uint8_t            obr;                                     // number of open brackets (preceding value)
    uint8_t            cbr;                                     // number of close brackets (following value, preceding operator)
    uint8_t            op;                                      // operator following
    int                value;                                   // constant value, or index to variable, or index to function
    int                fipslot;                                 // interface to functions
} EXPRESSION_CONTENT;

typedef struct
{
    EXPRESSION_CONTENT *    ec;
    int                     allocated;
} EXPRESSION_LIST;


typedef struct
{
    EXPRESSION_LIST **  argvp;                                  // arguments as expressions
} FIP_EXPR;

#define ACK                                         0x06
#define NACK                                        0x15

#define BUFLEN                                      256
#define MAX_KEYWORD_LEN                             256

#define isblank(ch)                                 ((ch) == ' ' || (ch) == '\t')

#define STATEMENT_ALLOC_GRANULARITY                 20
static STATEMENT            *                      statementp;
static int                                          statements_used = 0;
static int                                          statements_allocated = 0;

#define STATEMENT_STACK_DEPTH                       32

typedef struct
{
    int type;
    int idx;
} STATEMENT_STACK;

static STATEMENT_STACK                              statement_stack[STATEMENT_STACK_DEPTH];
static int                                          statement_stack_depth = 0;

#define STRING_ALLOC_GRANULARITY                    20
static unsigned             char **                 string_constants;
static int                                          string_constants_used = 0;
static int                                          string_constants_allocated = 0;

#define MAX_VARIABLE_NAME_LEN                       32

#define VARIABLES_ALLOC_GRANULARITY                 20
#define ARRAY_VARIABLES_ALLOC_GRANULARITY           20

#define LOCAL_VARIABLES_ALLOC_GRANULARITY           10
#define LOCAL_ARRAY_VARIABLES_ALLOC_GRANULARITY     5

typedef struct
{
    char                name[MAX_VARIABLE_NAME_LEN + 1];
    int                 line;
    union
    {
        int             int_value;
        unsigned char   byte_value;
        unsigned char * str_value;
    } v;
    int                 used_cnt;
    int                 set_cnt;
} VARIABLE;

typedef struct
{
    char                name[MAX_VARIABLE_NAME_LEN + 1];
    int                 line;
    union
    {
        int             int_value;
        unsigned char   byte_value;
        unsigned char * str_value;
    } v;
    int                 arraysize;
    int                 used_cnt;
    int                 set_cnt;
} ARRAY_VARIABLE;

static int                                          global_int_variables_used               = 0;
static int                                          global_int_variables_allocated          = 0;
static VARIABLE *                                   global_int_variables;

static int                                          global_int_array_variables_used         = 0;
static int                                          global_int_array_variables_allocated    = 0;
static ARRAY_VARIABLE *                             global_int_array_variables;

static int                                          global_byte_variables_used              = 0;
static int                                          global_byte_variables_allocated         = 0;
static VARIABLE *                                   global_byte_variables;

static int                                          global_byte_array_variables_used        = 0;
static int                                          global_byte_array_variables_allocated   = 0;
static ARRAY_VARIABLE *                             global_byte_array_variables;

static int                                          global_string_variables_used            = 0;
static int                                          global_string_variables_allocated       = 0;
static VARIABLE *                                   global_string_variables;

static int                                          global_string_array_variables_used      = 0;
static int                                          global_string_array_variables_allocated = 0;
static ARRAY_VARIABLE *                             global_string_array_variables;

static int                                          const_int_variables_used                = 0;
static int                                          const_int_variables_allocated           = 0;
static VARIABLE *                                   const_int_variables;

static int                                          const_string_variables_used             = 0;
static int                                          const_string_variables_allocated        = 0;
static VARIABLE *                                   const_string_variables;

#define FUNCTIONS_ALLOC_GRANULARITY                 10
#define MAX_FUNCTION_NAME_LEN                       32

#define ARGS_ALLOC_GRANULARITY                      4

typedef struct
{
    int                 line;
    char                name[MAX_FUNCTION_NAME_LEN + 1];
    int                 first_statement_idx;
    int                 return_type;
    int                 argc;
    int                 args_allocated;
    int *               argvars;
    int *               argtypes;

    int                 local_int_variables_used;
    int                 local_int_variables_allocated;
    VARIABLE *          local_int_variables;

    int                 local_byte_variables_used;
    int                 local_byte_variables_allocated;
    VARIABLE *          local_byte_variables;

    int                 local_string_variables_used;
    int                 local_string_variables_allocated;
    VARIABLE *          local_string_variables;

    int                 local_int_array_variables_used;
    int                 local_int_array_variables_allocated;
    ARRAY_VARIABLE *    local_int_array_variables;

    int                 local_byte_array_variables_used;
    int                 local_byte_array_variables_allocated;
    ARRAY_VARIABLE *    local_byte_array_variables;

    int                 local_string_array_variables_used;
    int                 local_string_array_variables_allocated;
    ARRAY_VARIABLE *    local_string_array_variables;
    int                 used_cnt;
} FUNCTION;

static int                                          in_function                         = 0;
static int                                          current_function_idx;

static int                                          functions_used                      = 0;
static int                                          functions_allocated                 = 0;
static FUNCTION *                                   functions;

typedef struct
{
    int             line;
    int             used_cnt;
    int             argc;
    int             needs_return_value;
    unsigned char   name[MAX_FUNCTION_NAME_LEN + 1];
} UNDEFINED_FUNCTION;

#define UNDEFINED_FUNCTION_ALLOC_GRANULARITY        20
static int                                          undefined_functions_used            = 0;
static int                                          undefined_functions_allocated       = 0;
static UNDEFINED_FUNCTION *                         undefined_functions;

#if defined(unix)
static struct termios                               tty;
static struct termios                               savetty;
static int                                          savehdl;
static int                                          tty_saved                           = 0;        // flag is serial tty mode has been saved
static int                                          nodelay_set                         = 0;        // flag if nodelay set on console
#endif

#define FIPSLOT_GRANULARITY                         40
static FIP_RUN **                                   fip_run_slots;
static FIP_EXPR **                                  fip_expr_slots;
static int                                          fipslots_used                       = 0;
static int                                          fipslots_allocated                  = 0;

#define POSTFIX_SLOT_GRANULARITY                    20

static POSTFIX_ELEMENT **                           postfix_slots;
static int                                          postfix_slots_used                  = 0;
static int                                          postfix_slots_allocated             = 0;

#define error_exit(errcode)                         longjmp (env, errcode)                          // errcode >= 1 - see also: setjmp(3)
static jmp_buf                                      env;

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * reset_globals - reset global variables
 *
 * This is neccessary under MINOS to make nicc reentrant
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
reset_globals (void)
{
    in_function             = 0;
    current_function_idx    = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * new_expression_list - allocate new expression list
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define EXPR_CONTENT_GRANULARITY                    4                                               // must be greater than 2 !

static EXPRESSION_LIST *
new_expression_list (char * ARG_UNUSED(file), int ARG_UNUSED(line))
{
    EXPRESSION_LIST *   el;

    el = alloc_calloc (file, line, 1, sizeof (EXPRESSION_LIST));

    if (el)
    {
        el->ec          = alloc_calloc (file, line, EXPR_CONTENT_GRANULARITY, sizeof (EXPRESSION_CONTENT));
        el->allocated   = EXPR_CONTENT_GRANULARITY;
    }
    else
    {
        // TODO
    }

    return el;
}

static EXPRESSION_LIST *
resize_expression_list (char * ARG_UNUSED(file), int ARG_UNUSED(line), EXPRESSION_LIST * el)
{
    el->ec = alloc_realloc (file, line, el->ec, (el->allocated + EXPR_CONTENT_GRANULARITY) * sizeof (EXPRESSION_CONTENT));

    if (el->ec)
    {
        memset (el->ec + el->allocated, 0, EXPR_CONTENT_GRANULARITY * sizeof (EXPRESSION_CONTENT));
        el->allocated += EXPR_CONTENT_GRANULARITY;
    }
    else
    {
        // TODO
    }

    return el;
}

static void
free_expression_list (char * ARG_UNUSED(file), int ARG_UNUSED(line), EXPRESSION_LIST * el)
{
    if (el->ec)
    {
        alloc_free (file, line, el->ec);
    }

    alloc_free (file, line, el);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * new_fipslot - allocate new fipslot
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
new_fipslot (int idx, int argc, EXPRESSION_LIST ** argvp)
{
    int     rtc = fipslots_used;

    if (fipslots_used == fipslots_allocated)
    {
        if (fipslots_allocated == 0)
        {
            fip_run_slots = alloc_calloc (__FILE__, __LINE__, FIPSLOT_GRANULARITY, sizeof (FIP_RUN *));

            if (! fip_run_slots)
            {
                return -1;
            }

            fip_expr_slots = alloc_calloc (__FILE__, __LINE__, FIPSLOT_GRANULARITY, sizeof (FIP_EXPR *));

            if (! fip_expr_slots)
            {
                return -1;
            }
        }
        else
        {
            fip_run_slots = alloc_realloc (__FILE__, __LINE__, fip_run_slots, (fipslots_allocated + FIPSLOT_GRANULARITY) * sizeof (FIP_RUN *));

            if (! fip_run_slots)
            {
                return -1;
            }

            memset (fip_run_slots + fipslots_allocated, 0, FIPSLOT_GRANULARITY * sizeof (FIP_RUN *));

            fip_expr_slots = alloc_realloc (__FILE__, __LINE__, fip_expr_slots, (fipslots_allocated + FIPSLOT_GRANULARITY) * sizeof (FIP_EXPR *));

            if (! fip_expr_slots)
            {
                return -1;
            }

            memset (fip_expr_slots + fipslots_allocated, 0, FIPSLOT_GRANULARITY * sizeof (FIP_EXPR *));
        }

        fipslots_allocated += FIPSLOT_GRANULARITY;
    }

    fip_run_slots[fipslots_used] = alloc_calloc (__FILE__, __LINE__, 1, sizeof (FIP_RUN));

    if (! fip_run_slots[fipslots_used])
    {
        return -1;
    }

    fip_expr_slots[fipslots_used] = alloc_calloc (__FILE__, __LINE__, 1, sizeof (FIP_EXPR));

    if (! fip_expr_slots[fipslots_used])
    {
        return -1;
    }

    fip_run_slots[fipslots_used]->func_idx    = idx;
    fip_run_slots[fipslots_used]->argc        = argc;
    fip_expr_slots[fipslots_used]->argvp      = argvp;

    fipslots_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_fipslots - free all allocated fipslots
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
free_fipslots (void)
{
    int     idx;
    int     argidx;

    for (idx = 0; idx < fipslots_used; idx++)
    {
        if (fip_run_slots[idx]->postfix_slotp)
        {
            alloc_free (__FILE__, __LINE__, fip_run_slots[idx]->postfix_slotp);
        }

        for (argidx = 0; argidx < fip_run_slots[idx]->argc; argidx++)
        {
            free_expression_list (__FILE__, __LINE__, fip_expr_slots[idx]->argvp[argidx]);
        }

        alloc_free (__FILE__, __LINE__, fip_expr_slots[idx]->argvp);
        alloc_free (__FILE__, __LINE__, fip_expr_slots[idx]);
        alloc_free (__FILE__, __LINE__, fip_run_slots[idx]);
    }

    alloc_free (__FILE__, __LINE__, fip_expr_slots);
    alloc_free (__FILE__, __LINE__, fip_run_slots);

    fip_run_slots       = (FIP_RUN **) NULL;
    fip_expr_slots      = (FIP_EXPR **) NULL;
    fipslots_used       = 0;
    fipslots_allocated  = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * size_fipslots - calculate size for all allocated fipslots - for statistics
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
size_t
size_fipslots (void)
{
    size_t  siz     = 0;
    int     idx;
    int     argidx;

    for (idx = 0; idx < fipslots_used; idx++)
    {
        if (fip_run_slots[idx]->postfix_slotp)
        {
            siz += sizeof (int);
        }

        for (argidx = 0; argidx < fip_run_slots[idx]->argc; argidx++)
        {
            siz += fip_expr_slots[idx]->argvp[argidx]->allocated * sizeof (EXPRESSION_CONTENT);
            siz += sizeof (EXPRESSION_LIST);
        }

        siz += sizeof (fip_expr_slots[idx]->argvp);
        siz += sizeof (FIP_EXPR);
        siz += sizeof (FIP_RUN);
    }

    siz += fipslots_allocated * sizeof (FIP_EXPR *);
    siz += fipslots_allocated * sizeof (FIP_RUN *);

    return siz;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * get_postfix_depth - get depth of a postfix element
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
get_postfix_depth (POSTFIX_ELEMENT * p)
{
    int idx = 0;

    while (p[idx].type != END)
    {
        idx++;
    }
    return idx + 1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * new_postfix_slot - allocate a new postfix slot
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
new_postfix_slot (POSTFIX_ELEMENT * postfixp)
{
    int     rtc = postfix_slots_used;
    int     depth;

    if (postfix_slots_used == postfix_slots_allocated)
    {
        if (postfix_slots_allocated == 0)
        {
            postfix_slots = alloc_calloc (__FILE__, __LINE__, POSTFIX_SLOT_GRANULARITY, sizeof (POSTFIX_ELEMENT *));

            if (! postfix_slots)
            {
                return -1;
            }
        }
        else
        {
            postfix_slots = alloc_realloc (__FILE__, __LINE__, postfix_slots, (postfix_slots_allocated + POSTFIX_SLOT_GRANULARITY) * sizeof (POSTFIX_ELEMENT *));

            if (! postfix_slots)
            {
                return -1;
            }

            memset (postfix_slots + postfix_slots_allocated, 0, POSTFIX_SLOT_GRANULARITY * sizeof (POSTFIX_ELEMENT *));
        }
        postfix_slots_allocated += POSTFIX_SLOT_GRANULARITY;
    }

    depth = get_postfix_depth (postfixp);

    postfix_slots[postfix_slots_used] = alloc_malloc (__FILE__, __LINE__, depth * sizeof (POSTFIX_ELEMENT));
    memcpy (postfix_slots[postfix_slots_used], postfixp, depth * sizeof (POSTFIX_ELEMENT));
    postfix_slots_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * size_postfix_slots - calculate size of all allocated postfix slots - for statistics
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static size_t
size_postfix_slots (void)
{
    size_t          siz;
    int             depth;
    int             i;

    siz =  postfix_slots_allocated * sizeof (POSTFIX_ELEMENT *);

    for (i = 0; i < postfix_slots_used; i++)
    {
        depth = get_postfix_depth (postfix_slots[i]);
        siz += depth * sizeof (POSTFIX_ELEMENT);
    }
    return siz;
}

void
expr_free_postfix_slots (void)
{
    int     idx;

    if (postfix_slots)
    {
        for (idx = 0; idx < postfix_slots_used; idx++)
        {
            alloc_free (__FILE__, __LINE__, postfix_slots[idx]);
        }

        alloc_free (__FILE__, __LINE__, postfix_slots);
    }

    postfix_slots           = (POSTFIX_ELEMENT **) NULL;
    postfix_slots_used      = 0;
    postfix_slots_allocated = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * push - push an expression onto stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
push (EXPRESSION_STACK * stackp, int x)
{
    if (stackp->stack_pointer < MAX_EXPR_EXPRESSION_STACK_DEPTH)
    {
        stackp->stack[stackp->stack_pointer] = x;
        stackp->stack_pointer++;
    }
    else
    {
        fprintf (stderr, "expression too complex, stack size exceeded\n");
        error_exit (1);
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * pop - pop an expression from stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
pop (EXPRESSION_STACK * stackp)
{
    if (stackp->stack_pointer == 0)
    {
        fprintf (stderr, "fatal: pop: stackpointer at bottom\n");
        error_exit (1);
    }

    stackp->stack_pointer--;
    return stackp->stack[stackp->stack_pointer];
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * peek - peek an expression on stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static char
peek (EXPRESSION_STACK * stackp)
{
    if (stackp->stack_pointer == 0)
    {
        fprintf (stderr, "fatal: peek: stackpointer at bottom\n");
        error_exit (1);
    }
    return (stackp->stack[stackp->stack_pointer - 1]);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * priority - get priority of a operator
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
priority (int operator)
{
    switch (operator)
    {
        case '+':
        case '-':               // pseudo prio if negation (unary operator), must be lowest. internally we use brackets.
        case '~':               // unary operator: pseudo prio, must be lowest. internally we use brackets.
            return 1;
        case '*':
            return 2;
        case '/':
            return 3;
        case '%':
            return 4;
        case '|':
            return 5;
        case '^':
            return 6;
        case '&':
            return 7;
        case '<':
        case '>':
            return 8;
        case ':':
            return 9;
    }
    return 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * infix2postfix - convert infix expression to postfix expression
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
infix2postfix (POSTFIX_ELEMENT * p, EXPRESSION_CONTENT * ec)
{
    EXPRESSION_STACK    stack;
    EXPRESSION_STACK *  stackp = &stack;
    unsigned int        expridx = 0;
    unsigned int        idx     = 0;
    int                 i;

    stackp->stack_pointer = 0;

    while (1)
    {
        int type        = ec[expridx].type;
        int brackets    = ec[expridx].obr;

        for (i = 0; i < brackets; i++)
        {
            push (stackp, '(');
        }

        if (type == EXPRESSION_CONTENT_TYPE_INT_CONSTANT)
        {
            p[idx].type         = OPERAND_INT_CONSTANT;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = -1;
            idx++;
        }
        if (type == EXPRESSION_CONTENT_TYPE_STRING_CONSTANT)
        {
            p[idx].type         = OPERAND_STRING_CONSTANT;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = -1;
            idx++;
        }
        else if (type == EXPRESSION_CONTENT_TYPE_LOCAL_INT_VARIABLE)
        {
            p[idx].type         = OPERAND_LOCAL_INT_VARIABLE;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = -1;
            idx++;
        }
        else if (type == EXPRESSION_CONTENT_TYPE_LOCAL_INT_ARRAY_VARIABLE)
        {
            p[idx].type         = OPERAND_LOCAL_INT_ARRAY_VARIABLE;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = ec[expridx].fipslot;
            idx++;
        }
        else if (type == EXPRESSION_CONTENT_TYPE_LOCAL_BYTE_VARIABLE)
        {
            p[idx].type         = OPERAND_LOCAL_BYTE_VARIABLE;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = -1;
            idx++;
        }
        else if (type == EXPRESSION_CONTENT_TYPE_LOCAL_BYTE_ARRAY_VARIABLE)
        {
            p[idx].type         = OPERAND_LOCAL_BYTE_ARRAY_VARIABLE;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = ec[expridx].fipslot;
            idx++;
        }
        else if (type == EXPRESSION_CONTENT_TYPE_LOCAL_STRING_VARIABLE)
        {
            p[idx].type         = OPERAND_LOCAL_STRING_VARIABLE;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = -1;
            idx++;
        }
        else if (type == EXPRESSION_CONTENT_TYPE_LOCAL_STRING_ARRAY_VARIABLE)
        {
            p[idx].type         = OPERAND_LOCAL_STRING_ARRAY_VARIABLE;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = ec[expridx].fipslot;
            idx++;
        }
        else if (type == EXPRESSION_CONTENT_TYPE_GLOBAL_INT_VARIABLE)
        {
            p[idx].type         = OPERAND_GLOBAL_INT_VARIABLE;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = -1;
            idx++;
        }
        else if (type == EXPRESSION_CONTENT_TYPE_GLOBAL_INT_ARRAY_VARIABLE)
        {
            p[idx].type         = OPERAND_GLOBAL_INT_ARRAY_VARIABLE;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = ec[expridx].fipslot;
            idx++;
        }
        else if (type == EXPRESSION_CONTENT_TYPE_GLOBAL_BYTE_VARIABLE)
        {
            p[idx].type         = OPERAND_GLOBAL_BYTE_VARIABLE;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = -1;
            idx++;
        }
        else if (type == EXPRESSION_CONTENT_TYPE_GLOBAL_BYTE_ARRAY_VARIABLE)
        {
            p[idx].type         = OPERAND_GLOBAL_BYTE_ARRAY_VARIABLE;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = ec[expridx].fipslot;
            idx++;
        }
        else if (type == EXPRESSION_CONTENT_TYPE_GLOBAL_STRING_VARIABLE)
        {
            p[idx].type         = OPERAND_GLOBAL_STRING_VARIABLE;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = -1;
            idx++;
        }
        else if (type == EXPRESSION_CONTENT_TYPE_GLOBAL_STRING_ARRAY_VARIABLE)
        {
            p[idx].type         = OPERAND_GLOBAL_STRING_ARRAY_VARIABLE;
            p[idx].value        = ec[expridx].value;
            p[idx].postfix_slot = ec[expridx].fipslot;
            idx++;
        }
        else if (type == EXPRESSION_CONTENT_TYPE_INTERN_FUNCTION ||
                 type == EXPRESSION_CONTENT_TYPE_EXTERN_FUNCTION ||
                 type == EXPRESSION_CONTENT_TYPE_UNDEFINED_FUNCTION)
        {
            POSTFIX_ELEMENT     postfix[MAX_POSTFIX_DEPTH];
            int                 fipslot;
            int                 argi;
            int                 argc;

            fipslot = ec[expridx].fipslot;

            argc = fip_run_slots[fipslot]->argc;

            if (argc > 0)
            {
                fip_run_slots[fipslot]->postfix_slotp = alloc_calloc (__FILE__, __LINE__, argc, sizeof (int));
            }

            for (argi = 0; argi < argc; argi++)
            {
                infix2postfix (postfix, fip_expr_slots[fipslot]->argvp[argi]->ec);
                fip_run_slots[fipslot]->postfix_slotp[argi] = new_postfix_slot (postfix);
            }

            if (type == EXPRESSION_CONTENT_TYPE_INTERN_FUNCTION)
            {
                p[idx].type = OPERAND_INTERN_FUNCTION;
            }
            else if (type == EXPRESSION_CONTENT_TYPE_EXTERN_FUNCTION)
            {
                p[idx].type = OPERAND_EXTERN_FUNCTION;
            }
            else // if (type == EXPRESSION_CONTENT_TYPE_UNDEFINED_FUNCTION)
            {
                p[idx].type = OPERAND_UNDEFINED_FUNCTION;
            }

            p[idx].value        = fipslot;
            p[idx].postfix_slot = -1;
            idx++;
        }

        brackets = ec[expridx].cbr;           // close brackets

        for (i = 0; i < brackets; i++)
        {
            /* repeat while stack is not empty... */
            while (stackp->stack_pointer > 0)
            {
                if (peek(stackp) != '(')
                {
                    p[idx].type = OPERATOR;
                    p[idx].value = pop(stackp);
                    p[idx].postfix_slot = -1;
                    idx++;
                }
                else
                {
                    pop(stackp);                // pop open bracket
                    break;
                }
            }
        }

        switch (ec[expridx].op)
        {
            case '+':
            case '-':
            case '*':
            case '/':
            case '%':
            case ':':
            case '<':
            case '>':
            case '|':
            case '&':
            case '^':
            case '~':
                /* if the scanned character is an operator and the stack is empty, push it to the stack */
                if (stackp->stack_pointer == 0)
                {
                    push (stackp, ec[expridx].op);
                }
                /* if stack is not empty... */
                else
                {
                    /* repeat while stack is not empty... */
                    while (stackp->stack_pointer > 0)
                    {
                        /* if precedence of stacked operator is higher than current one, then pop and add to postfix */
                        if (priority(peek(stackp)) > priority(ec[expridx].op))
                        {
                            p[idx].type         = OPERATOR;
                            p[idx].value        = pop(stackp);
                            p[idx].postfix_slot = -1;
                            idx++;
                        }
                        /* else break */
                        else
                        {
                            break;
                        }
                    }
                    /* finally push the current operator on stack */
                    push(stackp, ec[expridx].op);
                }
                break;
        }

        if (! ec[expridx].op)
        {
            break;
        }

        expridx++;
    }

    /* when all elements are read, pop everything from stack and add to postfix */
    while (stackp->stack_pointer > 0)
    {
        p[idx].type         = OPERATOR;
        p[idx].value        = pop(stackp);
        p[idx].postfix_slot = -1;
        idx++;
    }

    /* mark postfix end. */
    p[idx].type = END;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * opt_push - push function for optimizer
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
opt_push (EXPRESSION_STACK * stackp, int value, int type)
{
    if (stackp->stack_pointer < MAX_EXPR_EXPRESSION_STACK_DEPTH)
    {
        stackp->stack[stackp->stack_pointer] = value;
        stackp->type[stackp->stack_pointer] = type;
        stackp->stack_pointer++;
    }
    else
    {
        fprintf (stderr, "expression too complex, stack size exceeded\n");
        error_exit (1);
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * opt_pop - pop function for optimizer
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
opt_pop (EXPRESSION_STACK * stackp, int * valuep, int * typep)
{
    if (stackp->stack_pointer == 0)
    {
        fprintf (stderr, "fatal: run pop: stackpointer at bottom\n");
        error_exit (1);
    }

    stackp->stack_pointer--;

    *valuep = stackp->stack[stackp->stack_pointer];
    *typep  = stackp->type[stackp->stack_pointer];
    return stackp->stack_pointer;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * print_postfix_type_value - only for debugging or verbose output
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
print_postfix_type_value (int type, int value)
{
    if (type == OPERATOR)
    {
        fprintf (stderr, "o%c", value);
    }
    else if (type == OPERAND_INT_CONSTANT)
    {
        fprintf (stderr, "c%d", value);
    }
    else if (type == OPERAND_STRING_CONSTANT)
    {
        fprintf (stderr, "C%d", value);
    }
    else if (type == OPERAND_LOCAL_INT_VARIABLE)
    {
        fprintf (stderr, "v%d", value);
    }
    else if (type == OPERAND_LOCAL_INT_ARRAY_VARIABLE)
    {
        fprintf (stderr, "av%d", value);
    }
    else if (type == OPERAND_LOCAL_BYTE_VARIABLE)
    {
        fprintf (stderr, "b%d", value);
    }
    else if (type == OPERAND_LOCAL_BYTE_ARRAY_VARIABLE)
    {
        fprintf (stderr, "ab%d", value);
    }
    else if (type == OPERAND_LOCAL_STRING_VARIABLE)
    {
        fprintf (stderr, "s%d", value);
    }
    else if (type == OPERAND_LOCAL_STRING_ARRAY_VARIABLE)
    {
        fprintf (stderr, "as%d", value);
    }
    else if (type == OPERAND_GLOBAL_INT_VARIABLE)
    {
        fprintf (stderr, "V%d", value);
    }
    else if (type == OPERAND_GLOBAL_INT_ARRAY_VARIABLE)
    {
        fprintf (stderr, "aV%d", value);
    }
    else if (type == OPERAND_GLOBAL_BYTE_VARIABLE)
    {
        fprintf (stderr, "B%d", value);
    }
    else if (type == OPERAND_GLOBAL_BYTE_ARRAY_VARIABLE)
    {
        fprintf (stderr, "aB%d", value);
    }
    else if (type == OPERAND_GLOBAL_STRING_VARIABLE)
    {
        fprintf (stderr, "S%d", value);
    }
    else if (type == OPERAND_GLOBAL_STRING_ARRAY_VARIABLE)
    {
        fprintf (stderr, "aS%d", value);
    }
    else if (type == OPERAND_INTERN_FUNCTION)
    {
        fprintf (stderr, "f%d", value);
    }
    else if (type == OPERAND_EXTERN_FUNCTION)
    {
        fprintf (stderr, "F%d", value);
    }
    else
    {
        fprintf (stderr, "unhandled postfix type: %d\n", type);
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * print_postfix_slot - only for debugging or verbose output
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
print_postfix_slot (int slot)
{
    POSTFIX_ELEMENT *   p = postfix_slots[slot];
    int                 idx = 0;

    fprintf (stderr, "slot=%2d depth=%d ", slot, get_postfix_depth (p) - 1);

    while (p[idx].type != END)
    {
        print_postfix_type_value (p[idx].type, p[idx].value);
        idx++;
    }
    fputc ('\n', stderr);
    return OK;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate new string constant
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_string_constant (unsigned char * str)
{
    int     rtc;
    int     len = ustrlen (str);

    if (string_constants_used == string_constants_allocated)
    {
        if (string_constants_allocated == 0)
        {
            string_constants = alloc_calloc (__FILE__, __LINE__, STRING_ALLOC_GRANULARITY, sizeof (unsigned char *));

            if (! string_constants)
            {
                return -1;
            }
        }
        else
        {
            string_constants = alloc_realloc (__FILE__, __LINE__, string_constants,
                                                (string_constants_allocated + STRING_ALLOC_GRANULARITY) * sizeof (unsigned char *));

            if (! string_constants)
            {
                return -1;
            }

            memset (string_constants + string_constants_allocated, 0, STRING_ALLOC_GRANULARITY * sizeof (unsigned char *));
        }

        string_constants_allocated += STRING_ALLOC_GRANULARITY;
    }

    string_constants[string_constants_used] = alloc_malloc (__FILE__, __LINE__, len + 1);

    ustrcpy (string_constants[string_constants_used], str);

    rtc = string_constants_used;
    string_constants_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * reallocate new string constant (by optimizer)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
realloc_string_constant (int slot, unsigned char * new_str)
{
    int     rtc;
    int     len = ustrlen (new_str);

    string_constants[slot] = alloc_realloc (__FILE__, __LINE__, string_constants[slot], len + 1);
    ustrcpy (string_constants[slot], new_str);
    rtc = slot;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * deactivate string constant (by optimizer)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
deactivate_string_constant (int slot)
{
    alloc_free (__FILE__, __LINE__, string_constants[slot]);
    string_constants[slot] = (unsigned char *) 0;
}

static int  opt_cnt     = 0;
static int  hint_cnt    = 0;

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * optimize_postfix - optimize postfix (e.g. calculate constants)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
optimize_postfix (POSTFIX_ELEMENT * p)
{
    EXPRESSION_STACK    stack;
    EXPRESSION_STACK *  stackp = &stack;
    int                 op1;
    int                 op2;
    int                 type1;
    int                 type2;
    int                 result = 0;
    unsigned int        idx = 0;
    int                 ii;
    int                 hint;
    int                 opt_cnt_local = 0;

    stackp->stack_pointer = 0;

    // step 1: find constants and calculate them:
    while (p[idx].type != END)
    {
        if (p[idx].type != OPERATOR)
        {
            opt_push(stackp, p[idx].value, p[idx].type);
        }
        else
        {
            if (opt_pop (stackp, &op2, &type2) >= 0 &&
                opt_pop (stackp, &op1, &type1) >= 0)
            {
                if (p[idx].value == ':')
                {
                    if (type1 == OPERAND_STRING_CONSTANT && type2 == OPERAND_STRING_CONSTANT)
                    {
                        int             l1 = ustrlen (string_constants[op1]);
                        int             l2 = ustrlen (string_constants[op2]);
                        unsigned char   buf[l1 + l2 + 1];

                        ustrcpy (buf, string_constants[op1]);
                        ustrcpy (buf + l1, string_constants[op2]);

                        result = realloc_string_constant (op1, buf);
                        deactivate_string_constant (op2);

                        opt_push(stackp, result, OPERAND_STRING_CONSTANT);
                        opt_cnt_local++;
                    }
                    else if (type1 == OPERAND_STRING_CONSTANT && type2 == OPERAND_INT_CONSTANT)
                    {
                        char            vb[16];

                        sprintf (vb, "%d", op2);

                        int             l1 = ustrlen (string_constants[op1]);
                        int             l2 = ustrlen (vb);
                        unsigned char   buf[l1 + l2 + 1];

                        ustrcpy (buf, string_constants[op1]);
                        ustrcpy (buf + l1, vb);

                        result = realloc_string_constant (op1, buf);

                        opt_push(stackp, result, OPERAND_STRING_CONSTANT);
                        opt_cnt_local++;
                    }
                    else if (type1 == OPERAND_INT_CONSTANT && type2 == OPERAND_STRING_CONSTANT)
                    {
                        char            vb[16];

                        sprintf (vb, "%d", op1);

                        int             l1 = ustrlen (vb);
                        int             l2 = ustrlen (string_constants[op2]);
                        unsigned char   buf[l1 + l2 + 1];

                        ustrcpy (buf, vb);
                        ustrcpy (buf + l1, string_constants[op2]);

                        result = realloc_string_constant (op2, buf);

                        opt_push(stackp, result, OPERAND_STRING_CONSTANT);
                        opt_cnt_local++;
                    }
                    else if (type1 == OPERAND_INT_CONSTANT && type2 == OPERAND_INT_CONSTANT)
                    {
                        char            vb1[16];
                        char            vb2[16];

                        sprintf (vb1, "%d", op1);
                        sprintf (vb2, "%d", op2);

                        int             l1 = ustrlen (vb1);
                        int             l2 = ustrlen (vb2);
                        unsigned char   buf[l1 + l2 + 1];

                        ustrcpy (buf, vb1);
                        ustrcpy (buf + l1, vb2);

                        result = new_string_constant (buf);

                        opt_push(stackp, result, OPERAND_STRING_CONSTANT);
                        opt_cnt_local++;
                    }
                    else
                    {
                        opt_push (stackp, op1, type1);
                        opt_push (stackp, op2, type2);
                        opt_push(stackp, p[idx].value, p[idx].type);                        // push operator
                    }
                }
                else
                {
                    if (type1 == OPERAND_INT_CONSTANT && type2 == OPERAND_INT_CONSTANT)
                    {
                        switch (p[idx].value)
                        {
                            case '+':   result =            op1 +               op2;    break;
                            case '-':   result =            op1 -               op2;    break;
                            case '*':   result =            op1 *               op2;    break;
                            case '/':   result =            op1 /               op2;    break;
                            case '%':   result =            op1 %               op2;    break;
                            case '<':   result = (unsigned) op1 << (unsigned)   op2;    break;
                            case '>':   result = (unsigned) op1 >> (unsigned)   op2;    break;
                            case '&':   result = (unsigned) op1 &  (unsigned)   op2;    break;
                            case '|':   result = (unsigned) op1 |  (unsigned)   op2;    break;
                            case '^':   result = (unsigned) op1 ^  (unsigned)   op2;    break;
                            case '~':   result =                ~ ((unsigned)   op2);   break;
                        }
                        opt_push(stackp, result, OPERAND_INT_CONSTANT);
                        opt_cnt_local++;
                    }
                    else
                    {
                        opt_push (stackp, op1, type1);
                        opt_push (stackp, op2, type2);
                        opt_push(stackp, p[idx].value, p[idx].type);                        // push operator
                    }
                }
            }
            else
            {
                return -1;
            }

        }
        idx++;
    }

    if (opt_cnt_local)
    {
        for (ii = 0; ii < stackp->stack_pointer; ii++)
        {
            p[ii].type = stackp->type[ii];
            p[ii].value = stackp->stack[ii];
        }
        p[ii].type = END;
        p[ii].value = 0;

        opt_cnt += opt_cnt_local;
    }

    // step 2: find hints
    hint = OPTIMIZER_HINT_NONE;

    if (p[1].type == END)
    {
        switch (p[0].type)
        {
            case OPERAND_INT_CONSTANT:
            case OPERAND_STRING_CONSTANT:
            case OPERAND_LOCAL_STRING_VARIABLE:
            case OPERAND_GLOBAL_STRING_VARIABLE:
                hint = OPTIMIZER_HINT_CONST_NO_OP;                                  // postfix contains only const or string or string-var, but no operator
                break;
            case OPERAND_LOCAL_INT_VARIABLE:
                hint = OPTIMIZER_HINT_LOC_INT_NO_OP;                                // postfix contains only local int variable, but no operator
                break;
            case OPERAND_GLOBAL_INT_VARIABLE:
                hint = OPTIMIZER_HINT_GLOB_INT_NO_OP;                               // postfix contains only global int variable, but no operator
                break;
            case OPERAND_LOCAL_BYTE_VARIABLE:
                hint = OPTIMIZER_HINT_LOC_BYTE_NO_OP;                               // postfix contains only local int variable, but no operator
                break;
            case OPERAND_GLOBAL_BYTE_VARIABLE:
                hint = OPTIMIZER_HINT_GLOB_BYTE_NO_OP;                              // postfix contains only local int variable, but no operator
                break;
            case OPERAND_INTERN_FUNCTION:
                hint = OPTIMIZER_HINT_INT_FUNC_NO_OP;                               // postfix contains only call of intern function, but no operator
                break;
            case OPERAND_EXTERN_FUNCTION:
                hint = OPTIMIZER_HINT_EXT_FUNC_NO_OP;                               // postfix contains only call of extern function, but no operator
                break;
        }
    }
    else if (p[0].type == OPERAND_LOCAL_INT_VARIABLE && p[1].type == OPERAND_LOCAL_INT_VARIABLE && p[3].type == END)
    {
        hint = OPTIMIZER_HINT_LOC_INT_LOC_INT_OP;                                   // postfix contains local-int-var op local-int-var
    }
    else if (p[0].type == OPERAND_LOCAL_INT_VARIABLE && p[1].type == OPERAND_INT_CONSTANT && p[3].type == END)
    {
        hint = OPTIMIZER_HINT_LOC_INT_CONST_INT_OP;                                 // postfix contains local-int-var op const
    }
    else if (p[0].type == OPERAND_GLOBAL_INT_VARIABLE && p[1].type == OPERAND_GLOBAL_INT_VARIABLE && p[3].type == END)
    {
        hint = OPTIMIZER_HINT_GLOB_INT_GLOB_INT_OP;                                 // postfix contains global-int-var op global-int-var
    }
    else if (p[0].type == OPERAND_GLOBAL_INT_VARIABLE && p[1].type == OPERAND_INT_CONSTANT && p[3].type == END)
    {
        hint = OPTIMIZER_HINT_GLOB_INT_CONST_INT_OP;                                // postfix contains global-int-var op const
    }

    if (hint)
    {
        hint_cnt++;
    }

    stackp->stack_pointer = 0;
    return hint;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * dump_postfix - dump a postfix element into object file
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
dump_postfix (FILE * fp, POSTFIX_ELEMENT * p, int hint)
{
    int idx = 0;

    fprintf (fp, "%d %d ", get_postfix_depth (p) - 1, hint);

    while (p[idx].type != END)
    {
        if (p[idx].type == OPERATOR)
        {
            fprintf (fp, "o%c", p[idx].value);
        }
        else if (p[idx].type == OPERAND_INT_CONSTANT)
        {
            fprintf (fp, "c%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_STRING_CONSTANT)
        {
            fprintf (fp, "C%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_LOCAL_INT_VARIABLE)
        {
            fprintf (fp, "v%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_LOCAL_INT_ARRAY_VARIABLE)
        {
            fprintf (fp, "av%d[%d]", p[idx].value, p[idx].postfix_slot);
        }
        else if (p[idx].type == OPERAND_LOCAL_BYTE_VARIABLE)
        {
            fprintf (fp, "b%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_LOCAL_BYTE_ARRAY_VARIABLE)
        {
            fprintf (fp, "ab%d[%d]", p[idx].value, p[idx].postfix_slot);
        }
        else if (p[idx].type == OPERAND_LOCAL_STRING_VARIABLE)
        {
            fprintf (fp, "s%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_LOCAL_STRING_ARRAY_VARIABLE)
        {
            fprintf (fp, "as%d[%d]", p[idx].value, p[idx].postfix_slot);
        }
        else if (p[idx].type == OPERAND_GLOBAL_INT_VARIABLE)
        {
            fprintf (fp, "V%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_GLOBAL_INT_ARRAY_VARIABLE)
        {
            fprintf (fp, "aV%d[%d]", p[idx].value, p[idx].postfix_slot);
        }
        else if (p[idx].type == OPERAND_GLOBAL_BYTE_VARIABLE)
        {
            fprintf (fp, "B%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_GLOBAL_BYTE_ARRAY_VARIABLE)
        {
            fprintf (fp, "aB%d[%d]", p[idx].value, p[idx].postfix_slot);
        }
        else if (p[idx].type == OPERAND_GLOBAL_STRING_VARIABLE)
        {
            fprintf (fp, "S%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_GLOBAL_STRING_ARRAY_VARIABLE)
        {
            fprintf (fp, "aS%d[%d]", p[idx].value, p[idx].postfix_slot);
        }
        else if (p[idx].type == OPERAND_INTERN_FUNCTION)
        {
            fprintf (fp, "f%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_EXTERN_FUNCTION)
        {
            fprintf (fp, "F%d", p[idx].value);
        }
        else
        {
            fprintf (stderr, "unhandled postfix type: %d\n", p[idx].type);
            return ERR;
        }

        idx++;
    }
    putc (' ', fp);
    return OK;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * dump_postfix_slots - dump all postfix slots into object file
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
dump_postfix_slots (FILE * fp, int verbose)
{
    int i;
    int depth = 0;
    int hint;

    fprintf (fp, "%d\n", postfix_slots_used);

    for (i = 0; i < postfix_slots_used; i++)
    {
        if (verbose >= 2)
        {
            depth = get_postfix_depth (postfix_slots[i]);
            fprintf (stderr, "postfix:   ");
            print_postfix_slot (i);
        }

        hint = optimize_postfix (postfix_slots[i]);

        if (hint < 0)
        {
            return -1;
        }

        if (verbose)
        {
            if (depth > get_postfix_depth (postfix_slots[i]))
            {
                printf ("optimized: ");
                print_postfix_slot (i);
            }
        }

        dump_postfix (fp, postfix_slots[i], hint);
        putc ('\n', fp);
    }

    if (verbose)
    {
        fprintf (stderr, "postfix optimizations: %3d\n", opt_cnt);
        fprintf (stderr, "postfix opt hints:     %3d\n", hint_cnt);
    }

    return OK;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * dump_fipslots - dump all fipslots slots into object file
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
dump_fipslots (FILE * fp)
{
    int     i;
    int     j;

    fprintf (fp, "%d\n", fipslots_used);

    for (i = 0; i < fipslots_used; i++)
    {
        fprintf (fp, "%d %d ", fip_run_slots[i]->func_idx, fip_run_slots[i]->argc);

        for (j = 0; j < fip_run_slots[i]->argc; j++)
        {
            fprintf (fp, "%d ", fip_run_slots[i]->postfix_slotp[j]);
        }

        putc ('\n', fp);
    }

    return OK;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_string_constants - free all allocated string constants
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
free_string_constants (void)
{
    int     idx;

    for (idx = 0; idx < string_constants_used; idx++)
    {
        if (string_constants[idx])
        {
            alloc_free (__FILE__, __LINE__, string_constants[idx]);
        }
    }

    alloc_free (__FILE__, __LINE__, string_constants);

    string_constants            = (unsigned char **) NULL;
    string_constants_used       = 0;
    string_constants_allocated  = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * size_string_constants - size of all allocated string constants - only for statistics
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
size_string_constants (void)
{
    int     idx;
    int     siz = 0;

    for (idx = 0; idx < string_constants_used; idx++)
    {
        if (string_constants[idx])
        {
            siz += ustrlen (string_constants[idx]) + 1;
        }
    }

    siz += string_constants_allocated * sizeof (unsigned char *);
    return siz;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * skip blanks
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static unsigned char *
skip_blanks (unsigned char * s)
{
    while (isblank(*s))
    {
        s++;
    }
    return s;
}

enum
{
    KEYWORD_IS_EMPTY,                               //  0
    KEYWORD_IS_INT,                                 //  1
    KEYWORD_IS_STRING,                              //  2
    KEYWORD_IS_IDENTIFIER,                          //  3
    KEYWORD_IS_OPERATOR,                            //  4
    KEYWORD_IS_OPEN_BRACKET,                        //  5
    KEYWORD_IS_CLOSE_BRACKET,                       //  6
    KEYWORD_IS_EQUAL,                               //  7
    KEYWORD_IS_NOT_EQUAL,                           //  8
    KEYWORD_IS_LESS,                                //  9
    KEYWORD_IS_LESS_EQUAL,                          // 10
    KEYWORD_IS_GREATER,                             // 11
    KEYWORD_IS_GREATER_EQUAL,                       // 12
    KEYWORD_IS_ARGUMENT_SEPARATOR,                  // 13
    KEYWORD_IS_OPEN_SQUARE_BRACKET,                 // 14
    KEYWORD_IS_CLOSE_SQUARE_BRACKET,                // 15
};

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if character is letter or digit
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_letter_or_digit (int ch)
{
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
    {
        return 1;
    }
    return 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if character is hex digit
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_hex_digit (int ch)
{
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f'))
    {
        return 1;
    }
    return 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if character is binary digit
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_bin_digit (int ch)
{
    if (ch >= '0' && ch <= '1')
    {
        return 1;
    }
    return 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string is reserved keyword
 * Return values:
 *   len: is reserved keyword
 *   0: is not reserved keyword
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_reserved_keyword (const unsigned char * s, const char * keyword)
{
    int len = strlen (keyword);

    if (! ustrncmp (s, keyword, len)  && ! is_letter_or_digit (*(s + len)))
    {
        return len;
    }
    return 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents hex, decimal, binary or string
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_hex_dec_bin_str (unsigned char * s, int * skipp)
{
    int     len;

    if ((len = is_reserved_keyword (s, "HEX")) > 0)
    {
        *skipp = len;
        return (HEX_FORMAT);
    }
    else if ((len = is_reserved_keyword (s, "DEC")) > 0)
    {
        *skipp = len;
        return (DEC_FORMAT);
    }
    else if ((len = is_reserved_keyword (s, "DEC0")) > 0)
    {
        *skipp = len;
        return (DEC0_FORMAT);
    }
    else if ((len = is_reserved_keyword (s, "BIN")) > 0)
    {
        *skipp = len;
        return (BIN_FORMAT);
    }
    else if ((len = is_reserved_keyword (s, "STR")) > 0)
    {
        *skipp = len;
        return (STR_FORMAT);
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents mcurses attribute
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
typedef struct
{
    const char *    attr_name;
    uint16_t        attr;

} MCURSES_ATTR;

static const MCURSES_ATTR mcurses_attrs[] =
{
    {   "A_NORMAL",     A_NORMAL    },
    {   "A_UNDERLINE",  A_UNDERLINE },
    {   "A_REVERSE",    A_REVERSE   },
    {   "A_BLINK",      A_BLINK     },
    {   "A_BOLD",       A_BOLD      },
    {   "A_DIM",        A_DIM       },
    {   "A_STANDOUT",   A_STANDOUT  },

    {   "F_BLACK",      F_BLACK     },
    {   "F_RED",        F_RED       },
    {   "F_GREEN",      F_GREEN     },
    {   "F_BROWN",      F_BROWN     },
    {   "F_BLUE",       F_BLUE      },
    {   "F_MAGENTA",    F_MAGENTA   },
    {   "F_CYAN",       F_CYAN      },
    {   "F_WHITE",      F_WHITE     },
    {   "F_YELLOW",     F_YELLOW    },
    {   "F_BROWN",      F_BROWN     },
    {   "F_COLOR",      F_COLOR     },

    {   "B_BLACK",      B_BLACK     },
    {   "B_RED",        B_RED       },
    {   "B_GREEN",      B_GREEN     },
    {   "B_BROWN",      B_BROWN     },
    {   "B_BLUE",       B_BLUE      },
    {   "B_MAGENTA",    B_MAGENTA   },
    {   "B_CYAN",       B_CYAN      },
    {   "B_WHITE",      B_WHITE     },
    {   "B_YELLOW",     B_YELLOW    },
    {   "B_BROWN",      B_BROWN     },
    {   "B_COLOR",      B_COLOR     },
    {   (char *) 0,     0           }
};

static int
is_mcurses_attribute (unsigned char * s, int * skipp)
{
    int     idx;
    int     len;

    for (idx = 0; mcurses_attrs[idx].attr_name; idx++)
    {
        if ((len = is_reserved_keyword (s, mcurses_attrs[idx].attr_name)) > 0)
        {
            *skipp = len;
            return (mcurses_attrs[idx].attr);
        }
    }

    return -1;
}


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents color
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define COLOR_BLACK           0x0000
#define COLOR_BLUE            0x001F
#define COLOR_DARKBLUE        0x0007
#define COLOR_RED             0xF800
#define COLOR_DARKRED         0x3800
#define COLOR_GREEN           0x07E0
#define COLOR_DARKGREEN       0x01E0
#define COLOR_CYAN            (COLOR_GREEN      | COLOR_BLUE)
#define COLOR_DARKCYAN        (COLOR_DARKGREEN  | COLOR_DARKBLUE)
#define COLOR_MAGENTA         (COLOR_RED        | COLOR_BLUE)
#define COLOR_DARKMAGENTA     (COLOR_DARKRED    | COLOR_DARKBLUE)
#define COLOR_YELLOW          (COLOR_RED        | COLOR_GREEN)
#define COLOR_DARKYELLOW      (COLOR_DARKRED    | COLOR_DARKGREEN)
#define COLOR_WHITE           (COLOR_RED        | COLOR_GREEN       | COLOR_BLUE)
#define COLOR_GRAY            (COLOR_DARKRED    | COLOR_DARKGREEN   | COLOR_DARKBLUE)

static int
is_color (unsigned char * s, int * skipp)
{
    int     len;

    if ((len = is_reserved_keyword (s, "COLOR_BLACK")) > 0)
    {
        *skipp = len;
        return (COLOR_BLACK);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_BLUE")) > 0)
    {
        *skipp = len;
        return (COLOR_BLUE);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_DARKBLUE")) > 0)
    {
        *skipp = len;
        return (COLOR_DARKBLUE);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_RED")) > 0)
    {
        *skipp = len;
        return (COLOR_RED);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_DARKRED")) > 0)
    {
        *skipp = len;
        return (COLOR_DARKRED);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_GREEN")) > 0)
    {
        *skipp = len;
        return (COLOR_GREEN);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_DARKGREEN")) > 0)
    {
        *skipp = len;
        return (COLOR_DARKGREEN);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_CYAN")) > 0)
    {
        *skipp = len;
        return (COLOR_CYAN);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_DARKCYAN")) > 0)
    {
        *skipp = len;
        return (COLOR_DARKCYAN);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_MAGENTA")) > 0)
    {
        *skipp = len;
        return (COLOR_MAGENTA);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_DARKMAGENTA")) > 0)
    {
        *skipp = len;
        return (COLOR_DARKMAGENTA);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_YELLOW")) > 0)
    {
        *skipp = len;
        return (COLOR_YELLOW);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_DARKYELLOW")) > 0)
    {
        *skipp = len;
        return (COLOR_DARKYELLOW);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_WHITE")) > 0)
    {
        *skipp = len;
        return (COLOR_WHITE);
    }
    else if ((len = is_reserved_keyword (s, "COLOR_GRAY")) > 0)
    {
        *skipp = len;
        return (COLOR_GRAY);
    }
    return -1;
}


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents font
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define FONT_05x08           0
#define FONT_05x12           1
#define FONT_06x08           2
#define FONT_06x10           3
#define FONT_08x08           4
#define FONT_08x12           5
#define FONT_08x14           6
#define FONT_10x16           7
#define FONT_12x16           8
#define FONT_12x20           9
#define FONT_16x26          10
#define FONT_22x36          11
#define FONT_24x40          12
#define FONT_32x53          13

static int
is_font (unsigned char * s, int * skipp)
{
    int     len;

    if ((len = is_reserved_keyword (s, "FONT_05x08")) > 0)
    {
        *skipp = len;
        return FONT_05x08;
    }
    else if ((len = is_reserved_keyword (s, "FONT_05x12")) > 0)
    {
        *skipp = len;
        return FONT_05x12;
    }
    else if ((len = is_reserved_keyword (s, "FONT_06x08")) > 0)
    {
        *skipp = len;
        return FONT_06x08;
    }
    else if ((len = is_reserved_keyword (s, "FONT_06x10")) > 0)
    {
        *skipp = len;
        return FONT_06x10;
    }
    else if ((len = is_reserved_keyword (s, "FONT_08x08")) > 0)
    {
        *skipp = len;
        return FONT_08x08;
    }
    else if ((len = is_reserved_keyword (s, "FONT_08x12")) > 0)
    {
        *skipp = len;
        return FONT_08x12;
    }
    else if ((len = is_reserved_keyword (s, "FONT_08x14")) > 0)
    {
        *skipp = len;
        return FONT_08x14;
    }
    else if ((len = is_reserved_keyword (s, "FONT_10x16")) > 0)
    {
        *skipp = len;
        return FONT_10x16;
    }
    else if ((len = is_reserved_keyword (s, "FONT_12x16")) > 0)
    {
        *skipp = len;
        return FONT_12x16;
    }
    else if ((len = is_reserved_keyword (s, "FONT_12x20")) > 0)
    {
        *skipp = len;
        return FONT_12x20;
    }
    else if ((len = is_reserved_keyword (s, "FONT_16x26")) > 0)
    {
        *skipp = len;
        return FONT_16x26;
    }
    else if ((len = is_reserved_keyword (s, "FONT_22x36")) > 0)
    {
        *skipp = len;
        return FONT_22x36;
    }
    else if ((len = is_reserved_keyword (s, "FONT_24x40")) > 0)
    {
        *skipp = len;
        return FONT_24x40;
    }
    else if ((len = is_reserved_keyword (s, "FONT_32x53")) > 0)
    {
        *skipp = len;
        return FONT_32x53;
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents TRUE or FALSE
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_true_false (unsigned char * s, int * skipp)
{
    int     len;

    if ((len = is_reserved_keyword (s, "TRUE")) > 0)
    {
        *skipp = len;
        return '1';
    }

    if ((len = is_reserved_keyword (s, "FALSE")) > 0)
    {
        *skipp = len;
        return '0';
    }

    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents EOF
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_eof (unsigned char * s, int * skipp)
{
    int     len;

    if ((len = is_reserved_keyword (s, "EOF")) > 0)
    {
        *skipp = len;
        return TRUE;
    }

    return FALSE;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents SEEK_SET
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_seek_set (unsigned char * s, int * skipp)
{
    int     len;

    if ((len = is_reserved_keyword (s, "SEEK_SET")) > 0)
    {
        *skipp = len;
        return TRUE;
    }

    return FALSE;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents SEEK_CUR
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_seek_cur (unsigned char * s, int * skipp)
{
    int     len;

    if ((len = is_reserved_keyword (s, "SEEK_CUR")) > 0)
    {
        *skipp = len;
        return TRUE;
    }

    return FALSE;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents SEEK_END
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_seek_end (unsigned char * s, int * skipp)
{
    int     len;

    if ((len = is_reserved_keyword (s, "SEEK_END")) > 0)
    {
        *skipp = len;
        return TRUE;
    }

    return FALSE;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents HIGH or LOW
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_high_low (unsigned char * s, int * skipp)
{
    int len;

    if ((len = is_reserved_keyword (s, "LOW")) > 0)
    {
        *skipp = len;
        return '0';
    }

    if ((len = is_reserved_keyword (s, "HIGH")) > 0)
    {
        *skipp = len;
        return '1';
    }

    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents a GPIO port
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_gpio_port (unsigned char * s, int * skipp)
{
    if (! ustrncmp (s, "GPIO", 4) && *(s + 4) >= 'A' && *(s + 4) <= 'I' && !is_letter_or_digit (*(s + 5)))
    {
        *skipp = 5;
        return (*(s + 4) - 'A') + '0';
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents a GPIO mode: INPUT or OUTPUT
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_gpio_mode (unsigned char * s, int * skipp)
{
    int len;

    if ((len = is_reserved_keyword (s, "INPUT")) > 0)
    {
        *skipp = len;
        return '0';
    }

    if ((len = is_reserved_keyword (s, "OUTPUT")) > 0)
    {
        *skipp = len;
        return '1';
    }

    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents a GPIO pull mode: NOPULL, PULLUP, PULLDOWN, or PUSHPULL
 *                              BUTTON    mode: PULLUP, PULLDOWN, NOPULLUP, NOPULLDOWN
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_gpio_pull (unsigned char * s, int * skipp)
{
    int len;

    // input
    if ((len = is_reserved_keyword (s, "NOPULL")) > 0)
    {
        *skipp = len;
        return '0';
    }

    // input or button
    if ((len = is_reserved_keyword (s, "PULLUP")) > 0)
    {
        *skipp = len;
        return '1';
    }

    // input or button
    if ((len = is_reserved_keyword (s, "PULLDOWN")) > 0)
    {
        *skipp = len;
        return '2';
    }

    // button
    if ((len = is_reserved_keyword (s, "NOPULLUP")) > 0)
    {
        *skipp = len;
        return '3';
    }

    // button
    if ((len = is_reserved_keyword (s, "NOPULLDOWN")) > 0)
    {
        *skipp = len;
        return '4';
    }

    // output
    if ((len = is_reserved_keyword (s, "PUSHPULL")) > 0)
    {
        *skipp = len;
        return '0';
    }

    // output
    if ((len = is_reserved_keyword (s, "OPENDRAIN")) > 0)
    {
        *skipp = len;
        return '1';
    }

    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents an I2C channel
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_i2c_channel (unsigned char * s, int * skipp)
{
    if (! ustrncmp (s, "I2C", 3) && *(s + 3) >= '1' && *(s + 3) <= '3' && !is_letter_or_digit (*(s + 4)))
    {
        *skipp = 4;
        return (*(s + 3) - '0') + '0';              // I2C1 -> 1, I2C2 -> 2, I2C3 -> 3
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if string represents an UART number
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_uart_number (unsigned char * s, int * skipp)
{
    int     rtc = -1;
    if (! ustrncmp (s, "UART", 4) && *(s + 4) >= '1' && *(s + 4) <= '6' && !is_letter_or_digit (*(s + 5)))
    {
        *skipp = 5;
        rtc = (*(s + 4)) - 1;
    }
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check keyword
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
check_keyword (unsigned char * t, int line, unsigned char * s, unsigned char ** nextp, int minus_is_sign)
{
    int     skip;
    int     port;
    int     hilo;
    int     truefalse;
    int     format;
    int     mode;
    int     val;
    int     negative    = FALSE;
    int     len         = 0;
    int     rtc         = -1;

    s = skip_blanks (s);

    if (minus_is_sign && *s == '-')
    {
        negative = TRUE;
        s++;
    }

    if (! s || ! *s)
    {
        rtc = KEYWORD_IS_EMPTY;
        s = (unsigned char *) NULL;
    }
    else if (*s == '/' && *(s+1) == '/')
    {
        while (*s)
        {
            s++;
        }

        *t = '\0';
        rtc = KEYWORD_IS_EMPTY;
    }
    else if (*s == '0' && *(s + 1) == 'x' && is_hex_digit (*(s + 2)))
    {
        int value = 0;

        s += 2;

        while (is_hex_digit (*s))
        {
            value <<= 4;

            if (*s >= '0' && *s <= '9')
            {
                value += *s - '0';
            }
            else if (*s >= 'A' && *s <= 'F')
            {
                value += *s - 'A' + 10;
            }
            else
            {
                value += *s - 'a' + 10;
            }
            s++;
        }

        if (negative)
        {
            *t++ = '-';
        }

        sprintf ((char *) t, "%d", value);

        rtc = KEYWORD_IS_INT;
    }
    else if (*s == '0' && *(s + 1) == 'b' && is_bin_digit (*(s + 2)))
    {
        int value = 0;

        s += 2;

        while (is_bin_digit (*s))
        {
            value <<= 1;
            value += *s - '0';
            s++;
        }

        if (negative)
        {
            *t++ = '-';
        }

        sprintf ((char *) t, "%d", value);

        rtc = KEYWORD_IS_INT;
    }
    else if (*s >= '0' && *s <= '9')
    {
        if (negative)
        {
            *t++ = '-';
        }

        *t++ = *s++;
        len++;

        while (*s >= '0' && *s <= '9')
        {
            if (len < MAX_VARIABLE_NAME_LEN)
            {
                *t++ = *s++;
                len++;
            }
            else
            {
                fprintf (stderr, "error line %d: symbol too long, max. length is %d.\n", line, MAX_VARIABLE_NAME_LEN);
                return -1;
            }
        }

        *t = '\0';

        rtc = KEYWORD_IS_INT;
    }
    else if ((format = is_hex_dec_bin_str (s, &skip)) >= 0)
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        *t++ = format + '0';
        *t = '\0';

        rtc = KEYWORD_IS_INT;
    }
    else if ((val = is_mcurses_attribute (s, &skip)) >= 0)
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        sprintf ((char *) t, "%d", val);
        rtc = KEYWORD_IS_INT;
    }
    else if ((val = is_color (s, &skip)) >= 0)
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        sprintf ((char *) t, "%d", val);
        rtc = KEYWORD_IS_INT;
    }
    else if ((val = is_font (s, &skip)) >= 0)
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        sprintf ((char *) t, "%d", val);
        rtc = KEYWORD_IS_INT;
    }
    else if ((hilo = is_high_low (s, &skip)) >= 0)
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        *t++ = hilo;
        *t = '\0';

        rtc = KEYWORD_IS_INT;
    }
    else if ((truefalse = is_true_false (s, &skip)) >= 0)
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        *t++ = truefalse;
        *t = '\0';

        rtc = KEYWORD_IS_INT;
    }
    else if (is_eof (s, &skip))
    {
        s += skip;

        if (! negative)                 // NOT negative here!
        {
            *t++ = '-';
        }

        *t++ = '1';
        *t = '\0';

        rtc = KEYWORD_IS_INT;
    }
    else if (is_seek_set (s, &skip))
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        *t++ = SEEK_SET + '0';
        *t = '\0';

        rtc = KEYWORD_IS_INT;
    }
    else if (is_seek_cur (s, &skip))
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        *t++ = SEEK_CUR + '0';
        *t = '\0';

        rtc = KEYWORD_IS_INT;
    }
    else if (is_seek_end (s, &skip))
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        *t++ = SEEK_END + '0';
        *t = '\0';

        rtc = KEYWORD_IS_INT;
    }
    else if ((port = is_gpio_port (s, &skip)) >= 0)
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        *t++ = port;
        *t = '\0';

        rtc = KEYWORD_IS_INT;
    }
    else if ((mode = is_gpio_mode (s, &skip)) >= 0)
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        *t++ = mode;
        *t = '\0';

        rtc = KEYWORD_IS_INT;
    }
    else if ((mode = is_gpio_pull (s, &skip)) >= 0)
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        *t++ = mode;
        *t = '\0';

        rtc = KEYWORD_IS_INT;
    }
    else if ((port = is_i2c_channel (s, &skip)) >= 0)
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        *t++ = port;
        *t = '\0';

        rtc = KEYWORD_IS_INT;
    }
    else if ((port = is_uart_number (s, &skip)) >= 0)
    {
        s += skip;

        if (negative)
        {
            *t++ = '-';
        }

        *t++ = port;
        *t = '\0';

        rtc = KEYWORD_IS_INT;
    }
    else if (*s == '"')
    {
        s++;

        if (negative)
        {
            *t++ = '-';
        }

        while (*s != '"' && *s)
        {
            if (len < MAX_KEYWORD_LEN)
            {
                *t++ = *s++;
                len++;
            }
            else
            {
                fprintf (stderr, "error line %d: string too long, max. length is %d.\n", line, MAX_KEYWORD_LEN);
                return -1;
            }
        }

        *t = '\0';

        if (*s)
        {
            s++;
            rtc = KEYWORD_IS_STRING;
        }
        else
        {
            fprintf (stderr, "error line %d: unterminated string.\n", line);
            rtc = -1;
        }
    }
    else if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'))
    {
        if (negative)
        {
            *t++ = '-';
        }

        *t++ = *s++;
        len++;

        while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_' || *s == '.')
        {
            *t++ = *s++;
        }

        *t = '\0';

        rtc = KEYWORD_IS_IDENTIFIER;
    }
    else if (*s == '+' || *s == '-' || *s == '/' || *s == '*' || *s == '%' || *s == ':' || *s == '&' || *s == '|' || *s == '^')
    {
        *t++ = *s++;
        *t = '\0';
        rtc = KEYWORD_IS_OPERATOR;
    }
    else if (*s == '~')
    {
        *t++ = *s++;
        *t = '\0';
        rtc = KEYWORD_IS_OPERATOR;
    }
    else if ((*s == '<' && *(s+1) == '<') || (*s == '>' && *(s+1) == '>'))
    {
        *t++ = *s++;                    // reduce '<<' to '<', '>>' to '>'
        s++;                            // skip 2nd '<' or '>'
        *t = '\0';
        rtc = KEYWORD_IS_OPERATOR;
    }
    else if (*s == '(')
    {
        *t++ = *s++;
        *t = '\0';
        rtc = KEYWORD_IS_OPEN_BRACKET;
    }
    else if (*s == ')')
    {
        *t++ = *s++;
        *t = '\0';
        rtc = KEYWORD_IS_CLOSE_BRACKET;
    }
    else if (*s == '[')
    {
        *t++ = *s++;
        *t = '\0';
        rtc = KEYWORD_IS_OPEN_SQUARE_BRACKET;
    }
    else if (*s == ']')
    {
        *t++ = *s++;
        *t = '\0';
        rtc = KEYWORD_IS_CLOSE_SQUARE_BRACKET;
    }
    else if (*s == '=')
    {
        *t++ = *s++;
        *t = '\0';
        rtc = KEYWORD_IS_EQUAL;
    }
    else if (*s == '!' && *(s + 1) == '=' )
    {
        *t++ = *s++;
        *t++ = *s++;
        *t = '\0';
        rtc = KEYWORD_IS_NOT_EQUAL;
    }
    else if (*s == '<')
    {
        *t++ = *s++;

        if (*s == '=')
        {
            *t++ = *s++;
            rtc = KEYWORD_IS_LESS_EQUAL;
        }
        else
        {
            rtc = KEYWORD_IS_LESS;
        }

        *t = '\0';
    }
    else if (*s == '>')
    {
        *t++ = *s++;

        if (*s == '=')
        {
            *t++ = *s++;
            rtc = KEYWORD_IS_GREATER_EQUAL;
        }
        else
        {
            rtc = KEYWORD_IS_GREATER;
        }

        *t = '\0';
    }
    else if (*s == ',')
    {
        *t++ = *s++;
        *t = '\0';
        rtc = KEYWORD_IS_ARGUMENT_SEPARATOR;
    }
    else
    {
        fprintf (stderr, "error line %d: '%s' unexpected.\n", line, s);
        rtc = -1;
    }

    if (nextp)
    {
        if (s)
        {
            *nextp = skip_blanks (s);
        }
        else
        {
            *nextp = (unsigned char *) "";
        }
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find function
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_function (unsigned char * name)
{
    int     fidx;

    for (fidx = 0; fidx < functions_used; fidx++)
    {
        if (! ustrncmp (functions[fidx].name, name, MAX_FUNCTION_NAME_LEN))
        {
            return fidx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find undefined function
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_undefined_function (unsigned char * name)
{
    int         fidx;

    for (fidx = 0; fidx < functions_used; fidx++)
    {
        if (! ustrcmp (name, functions[fidx].name))
        {
            return fidx;
        }
    }

    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate data for an undefined function
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_undefined_function (unsigned char * name, int line)
{
    int     rtc;

    if (undefined_functions_used == undefined_functions_allocated)
    {
        if (undefined_functions_allocated == 0)
        {
            undefined_functions = alloc_calloc (__FILE__, __LINE__, UNDEFINED_FUNCTION_ALLOC_GRANULARITY, sizeof (UNDEFINED_FUNCTION));

            if (! undefined_functions)
            {
                return -1;
            }
        }
        else
        {
            undefined_functions = alloc_realloc (__FILE__, __LINE__, undefined_functions,
                                        (undefined_functions_allocated + UNDEFINED_FUNCTION_ALLOC_GRANULARITY) * sizeof (UNDEFINED_FUNCTION));

            if (! undefined_functions)
            {
                return -1;
            }

            memset (undefined_functions + undefined_functions_allocated, 0, UNDEFINED_FUNCTION_ALLOC_GRANULARITY * sizeof (UNDEFINED_FUNCTION));
        }

        undefined_functions_allocated += UNDEFINED_FUNCTION_ALLOC_GRANULARITY;
    }

    ustrncpy (undefined_functions[undefined_functions_used].name, name, MAX_FUNCTION_NAME_LEN);
    undefined_functions[undefined_functions_used].line      = line;
    undefined_functions[undefined_functions_used].used_cnt  = 0;

    rtc = undefined_functions_used;
    undefined_functions_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_undefined_functions - free allocated undefined functions
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
free_undefined_functions (void)
{
    alloc_free (__FILE__, __LINE__, undefined_functions);

    undefined_functions             = (UNDEFINED_FUNCTION *) NULL;
    undefined_functions_used        = 0;
    undefined_functions_allocated   = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * size_undefined_functions - calculate size of allocated undefined functions - only for statistics
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
size_undefined_functions (void)
{
    int siz = undefined_functions_allocated * sizeof (UNDEFINED_FUNCTION);
    return siz;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate data for a defined function
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_function (unsigned char * name, int line, int type, int statement_idx)
{
    int     rtc;

    if (functions_used == functions_allocated)
    {
        if (functions_allocated == 0)
        {
            functions = alloc_calloc (__FILE__, __LINE__, FUNCTIONS_ALLOC_GRANULARITY, sizeof (FUNCTION));

            if (! functions)
            {
                return -1;
            }
        }
        else
        {
            functions = alloc_realloc (__FILE__, __LINE__, functions, (functions_allocated + FUNCTIONS_ALLOC_GRANULARITY) * sizeof (FUNCTION));

            if (! functions)
            {
                return -1;
            }

            memset (functions + functions_allocated, 0, FUNCTIONS_ALLOC_GRANULARITY * sizeof (FUNCTION));
        }

        functions_allocated += FUNCTIONS_ALLOC_GRANULARITY;
    }

    ustrncpy (functions[functions_used].name, name, MAX_FUNCTION_NAME_LEN);
    functions[functions_used].return_type = type;
    functions[functions_used].first_statement_idx = statement_idx;
    functions[functions_used].line = line;

    if (! ustrcmp (name, "main"))
    {
        functions[functions_used].used_cnt = 1;
    }
    else
    {
        functions[functions_used].used_cnt = 0;
    }

    rtc = functions_used;
    functions_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate data for a function argument
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_arg (FUNCTION * funcp, int argvaridx, int argtype)
{
    int     rtc;

    if (funcp->argc == funcp->args_allocated)
    {
        if (funcp->args_allocated == 0)
        {
            funcp->argvars = alloc_calloc (__FILE__, __LINE__, ARGS_ALLOC_GRANULARITY, sizeof (int));

            if (! funcp->argvars)
            {
                return -1;
            }

            funcp->argtypes = alloc_calloc (__FILE__, __LINE__, ARGS_ALLOC_GRANULARITY, sizeof (int));

            if (! funcp->argtypes)
            {
                return -1;
            }
        }
        else
        {
            funcp->argvars = alloc_realloc (__FILE__, __LINE__, funcp->argvars, (funcp->args_allocated + ARGS_ALLOC_GRANULARITY) * sizeof (int));

            if (! funcp->argvars)
            {
                return -1;
            }

            memset (funcp->argvars + funcp->args_allocated, 0, ARGS_ALLOC_GRANULARITY * sizeof (int));

            funcp->argtypes = alloc_realloc (__FILE__, __LINE__, funcp->argtypes, (funcp->args_allocated + ARGS_ALLOC_GRANULARITY) * sizeof (int));

            if (! funcp->argtypes)
            {
                return -1;
            }

            memset (funcp->argtypes + funcp->args_allocated, 0, ARGS_ALLOC_GRANULARITY * sizeof (int));
        }

        funcp->args_allocated += ARGS_ALLOC_GRANULARITY;
    }

    funcp->argvars[funcp->argc]     = argvaridx;
    funcp->argtypes[funcp->argc]    = argtype;

    rtc = funcp->argc;
    funcp->argc++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_functions - free allocated functions
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
free_functions (void)
{
    int     idx;

    for (idx = 0; idx < functions_used; idx++)
    {
        alloc_free (__FILE__, __LINE__, functions[idx].local_int_variables);
        alloc_free (__FILE__, __LINE__, functions[idx].local_int_array_variables);
        alloc_free (__FILE__, __LINE__, functions[idx].local_string_variables);
        alloc_free (__FILE__, __LINE__, functions[idx].local_string_array_variables);
        alloc_free (__FILE__, __LINE__, functions[idx].local_byte_variables);
        alloc_free (__FILE__, __LINE__, functions[idx].local_byte_array_variables);
        alloc_free (__FILE__, __LINE__, functions[idx].argvars);
        alloc_free (__FILE__, __LINE__, functions[idx].argtypes);
    }

    alloc_free (__FILE__, __LINE__, functions);

    functions             = (FUNCTION *) NULL;
    functions_used        = 0;
    functions_allocated   = 0;
}


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * size_functions - calculate size of all allocated functions - only for statistics
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
size_functions (void)
{
    int     idx;
    int     siz = 0;

    for (idx = 0; idx < functions_used; idx++)
    {
        siz += functions[idx].local_int_variables_allocated * sizeof (VARIABLE);
        siz += functions[idx].local_int_array_variables_allocated * sizeof (ARRAY_VARIABLE);
        siz += functions[idx].local_string_variables_allocated * sizeof (VARIABLE);
        siz += functions[idx].local_string_array_variables_allocated * sizeof (ARRAY_VARIABLE);
        siz += functions[idx].local_byte_variables_allocated * sizeof (VARIABLE);
        siz += functions[idx].local_byte_array_variables_allocated * sizeof (ARRAY_VARIABLE);

        siz += functions[idx].args_allocated * sizeof (int);
        siz += functions[idx].args_allocated * sizeof (int);
    }

    siz += functions_allocated * sizeof (FUNCTION);
    return siz;
}


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a global integer variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_global_int_variable (unsigned char * name)
{
    int     idx;

    for (idx = 0; idx < global_int_variables_used; idx++)
    {
        if (! ustrncmp (global_int_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a global integer array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_global_int_array_variable (unsigned char * name)
{
    int     idx;

    for (idx = 0; idx < global_int_array_variables_used; idx++)
    {
        if (! ustrncmp (global_int_array_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a global byte variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_global_byte_variable (unsigned char * name)
{
    int     idx;

    for (idx = 0; idx < global_byte_variables_used; idx++)
    {
        if (! ustrncmp (global_byte_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a global byte array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_global_byte_array_variable (unsigned char * name)
{
    int     idx;

    for (idx = 0; idx < global_byte_array_variables_used; idx++)
    {
        if (! ustrncmp (global_byte_array_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a local const integer variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_local_const_int_variable (FUNCTION * funcp, unsigned char * name)
{
    unsigned char   const_name[MAX_VARIABLE_NAME_LEN * 2 + 2];
    int             idx;

    sprintf ((char *) const_name, "%s.%s", funcp->name, name);

    for (idx = 0; idx < const_int_variables_used; idx++)
    {
        if (! ustrncmp (const_int_variables[idx].name, const_name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a global const integer variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_global_const_int_variable (unsigned char * name)
{
    int             idx;

    for (idx = 0; idx < const_int_variables_used; idx++)
    {
        if (! ustrncmp (const_int_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a static integer variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_static_int_variable (FUNCTION * funcp, unsigned char * name)
{
    unsigned char   static_name[MAX_VARIABLE_NAME_LEN * 2 + 2];
    int             idx;

    sprintf ((char *) static_name, "%s.%s", funcp->name, name);

    for (idx = 0; idx < global_int_variables_used; idx++)
    {
        if (! ustrncmp (global_int_variables[idx].name, static_name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a static integer array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_static_int_array_variable (FUNCTION * funcp, unsigned char * name)
{
    unsigned char   static_name[MAX_VARIABLE_NAME_LEN * 2 + 2];
    int             idx;

    sprintf ((char *) static_name, "%s.%s", funcp->name, name);

    for (idx = 0; idx < global_int_array_variables_used; idx++)
    {
        if (! ustrncmp (global_int_array_variables[idx].name, static_name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a static byte variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_static_byte_variable (FUNCTION * funcp, unsigned char * name)
{
    unsigned char   static_name[MAX_VARIABLE_NAME_LEN * 2 + 2];
    int             idx;

    sprintf ((char *) static_name, "%s.%s", funcp->name, name);

    for (idx = 0; idx < global_byte_variables_used; idx++)
    {
        if (! ustrncmp (global_byte_variables[idx].name, static_name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a static byte array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_static_byte_array_variable (FUNCTION * funcp, unsigned char * name)
{
    unsigned char   static_name[MAX_VARIABLE_NAME_LEN * 2 + 2];
    int             idx;

    sprintf ((char *) static_name, "%s.%s", funcp->name, name);

    for (idx = 0; idx < global_byte_array_variables_used; idx++)
    {
        if (! ustrncmp (global_byte_array_variables[idx].name, static_name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate data for a global integer variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_global_int_variable (unsigned char * name, int line)
{
    int     rtc;

    if (global_int_variables_used == global_int_variables_allocated)
    {
        if (global_int_variables_allocated == 0)
        {
            global_int_variables = alloc_calloc (__FILE__, __LINE__, VARIABLES_ALLOC_GRANULARITY, sizeof (VARIABLE));

            if (! global_int_variables)
            {
                return -1;
            }
        }
        else
        {
            global_int_variables = alloc_realloc (__FILE__, __LINE__, global_int_variables,
                                        (global_int_variables_allocated + VARIABLES_ALLOC_GRANULARITY) * sizeof (VARIABLE));

            if (! global_int_variables)
            {
                return -1;
            }

            memset (global_int_variables + global_int_variables_allocated, 0, VARIABLES_ALLOC_GRANULARITY * sizeof (VARIABLE));
        }

        global_int_variables_allocated += VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (global_int_variables[global_int_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    global_int_variables[global_int_variables_used].line        = line;
    global_int_variables[global_int_variables_used].used_cnt    = 0;

    rtc = global_int_variables_used;
    global_int_variables_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_global_int_variables - free global integer variables
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
free_global_int_variables (void)
{
    alloc_free (__FILE__, __LINE__, global_int_variables);

    global_int_variables            = 0;
    global_int_variables_used       = 0;
    global_int_variables_allocated  = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate data for a global integer array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_global_int_array_variable (unsigned char * name, int arraysize, int line)
{
    int     rtc;

    if (global_int_array_variables_used == global_int_array_variables_allocated)
    {
        if (global_int_array_variables_allocated == 0)
        {
            global_int_array_variables = alloc_calloc (__FILE__, __LINE__, ARRAY_VARIABLES_ALLOC_GRANULARITY, sizeof (ARRAY_VARIABLE));

            if (! global_int_array_variables)
            {
                return -1;
            }
        }
        else
        {
            global_int_array_variables = alloc_realloc (__FILE__, __LINE__, global_int_array_variables,
                                                    (global_int_array_variables_allocated + ARRAY_VARIABLES_ALLOC_GRANULARITY) * sizeof (ARRAY_VARIABLE));

            if (! global_int_array_variables)
            {
                return -1;
            }

            memset (global_int_array_variables + global_int_array_variables_allocated, 0, ARRAY_VARIABLES_ALLOC_GRANULARITY * sizeof (ARRAY_VARIABLE));
        }

        global_int_array_variables_allocated += ARRAY_VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (global_int_array_variables[global_int_array_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    global_int_array_variables[global_int_array_variables_used].line        = line;
    global_int_array_variables[global_int_array_variables_used].arraysize   = arraysize;
    global_int_array_variables[global_int_array_variables_used].used_cnt    = 0;

    rtc = global_int_array_variables_used;
    global_int_array_variables_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_global_int_array_variables - free global integer array variables
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
free_global_int_array_variables (void)
{
    alloc_free (__FILE__, __LINE__, global_int_array_variables);

    global_int_array_variables            = 0;
    global_int_array_variables_used       = 0;
    global_int_array_variables_allocated  = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate data for a global const integer variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_const_int_variable (unsigned char * name, int line)
{
    int     rtc;

    if (const_int_variables_used == const_int_variables_allocated)
    {
        if (const_int_variables_allocated == 0)
        {
            const_int_variables = alloc_calloc (__FILE__, __LINE__, VARIABLES_ALLOC_GRANULARITY, sizeof (VARIABLE));

            if (! const_int_variables)
            {
                return -1;
            }
        }
        else
        {
            const_int_variables = alloc_realloc (__FILE__, __LINE__, const_int_variables,
                                                        (const_int_variables_allocated + VARIABLES_ALLOC_GRANULARITY) * sizeof (VARIABLE));

            if (! const_int_variables)
            {
                return -1;
            }

            memset (const_int_variables + const_int_variables_allocated, 0, VARIABLES_ALLOC_GRANULARITY * sizeof (VARIABLE));
        }

        const_int_variables_allocated += VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (const_int_variables[const_int_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    const_int_variables[const_int_variables_used].line          = line;
    const_int_variables[const_int_variables_used].used_cnt      = 0;

    rtc = const_int_variables_used;
    const_int_variables_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_const_int_variables - free const integer variables
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
free_const_int_variables (void)
{
    alloc_free (__FILE__, __LINE__, const_int_variables);

    const_int_variables            = 0;
    const_int_variables_used       = 0;
    const_int_variables_allocated  = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate data for a global byte variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_global_byte_variable (unsigned char * name, int line)
{
    int     rtc;

    if (global_byte_variables_used == global_byte_variables_allocated)
    {
        if (global_byte_variables_allocated == 0)
        {
            global_byte_variables = alloc_calloc (__FILE__, __LINE__, VARIABLES_ALLOC_GRANULARITY, sizeof (VARIABLE));

            if (! global_byte_variables)
            {
                return -1;
            }
        }
        else
        {
            global_byte_variables = alloc_realloc (__FILE__, __LINE__, global_byte_variables,
                                                    (global_byte_variables_allocated + VARIABLES_ALLOC_GRANULARITY) * sizeof (VARIABLE));

            if (! global_byte_variables)
            {
                return -1;
            }

            memset (global_byte_variables + global_byte_variables_allocated, 0, VARIABLES_ALLOC_GRANULARITY * sizeof (VARIABLE));
        }

        global_byte_variables_allocated += VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (global_byte_variables[global_byte_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    global_byte_variables[global_byte_variables_used].line        = line;
    global_byte_variables[global_byte_variables_used].used_cnt    = 0;

    rtc = global_byte_variables_used;
    global_byte_variables_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_global_byte_variables - free global byte variables
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
free_global_byte_variables (void)
{
    alloc_free (__FILE__, __LINE__, global_byte_variables);

    global_byte_variables            = 0;
    global_byte_variables_used       = 0;
    global_byte_variables_allocated  = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate data for a global byte array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_global_byte_array_variable (unsigned char * name, int arraysize, int line)
{
    int     rtc;

    if (global_byte_array_variables_used == global_byte_array_variables_allocated)
    {
        if (global_byte_array_variables_allocated == 0)
        {
            global_byte_array_variables = alloc_calloc (__FILE__, __LINE__, ARRAY_VARIABLES_ALLOC_GRANULARITY, sizeof (ARRAY_VARIABLE));

            if (! global_byte_array_variables)
            {
                return -1;
            }
        }
        else
        {
            global_byte_array_variables = alloc_realloc (__FILE__, __LINE__, global_byte_array_variables,
                                                    (global_byte_array_variables_allocated + ARRAY_VARIABLES_ALLOC_GRANULARITY) * sizeof (ARRAY_VARIABLE));

            if (! global_byte_array_variables)
            {
                return -1;
            }

            memset (global_byte_array_variables + global_byte_array_variables_allocated, 0, ARRAY_VARIABLES_ALLOC_GRANULARITY * sizeof (ARRAY_VARIABLE));
        }

        global_byte_array_variables_allocated += ARRAY_VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (global_byte_array_variables[global_byte_array_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    global_byte_array_variables[global_byte_array_variables_used].line        = line;
    global_byte_array_variables[global_byte_array_variables_used].arraysize   = arraysize;
    global_byte_array_variables[global_byte_array_variables_used].used_cnt    = 0;

    rtc = global_byte_array_variables_used;
    global_byte_array_variables_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_global_byte_array_variables - free global byte array variables
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
free_global_byte_array_variables (void)
{
    alloc_free (__FILE__, __LINE__, global_byte_array_variables);

    global_byte_array_variables            = 0;
    global_byte_array_variables_used       = 0;
    global_byte_array_variables_allocated  = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a global string variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_global_string_variable (unsigned char * name)
{
    int     idx;

    for (idx = 0; idx < global_string_variables_used; idx++)
    {
        if (! ustrncmp (global_string_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a global string array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_global_string_array_variable (unsigned char * name)
{
    int     idx;

    for (idx = 0; idx < global_string_array_variables_used; idx++)
    {
        if (! ustrncmp (global_string_array_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a local const string variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_local_const_string_variable (FUNCTION * funcp, unsigned char * name)
{
    unsigned char   const_name[MAX_VARIABLE_NAME_LEN * 2 + 2];
    int             idx;

    sprintf ((char *) const_name, "%s.%s", funcp->name, name);

    for (idx = 0; idx < const_string_variables_used; idx++)
    {
        if (! ustrncmp (const_string_variables[idx].name, const_name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a global const string variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_global_const_string_variable (unsigned char * name)
{
    int             idx;

    for (idx = 0; idx < const_string_variables_used; idx++)
    {
        if (! ustrncmp (const_string_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a static string variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_static_string_variable (FUNCTION * funcp, unsigned char * name)
{
    unsigned char   static_name[MAX_VARIABLE_NAME_LEN * 2 + 2];
    int             idx;

    sprintf ((char *) static_name, "%s.%s", funcp->name, name);

    for (idx = 0; idx < global_string_variables_used; idx++)
    {
        if (! ustrncmp (global_string_variables[idx].name, static_name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a static string array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_static_string_array_variable (FUNCTION * funcp, unsigned char * name)
{
    unsigned char   static_name[MAX_VARIABLE_NAME_LEN * 2 + 2];
    int             idx;

    sprintf ((char *) static_name, "%s.%s", funcp->name, name);

    for (idx = 0; idx < global_string_array_variables_used; idx++)
    {
        if (! ustrncmp (global_string_array_variables[idx].name, static_name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate data for a global string variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_global_string_variable (unsigned char * name, int line)
{
    int     rtc;

    if (global_string_variables_used == global_string_variables_allocated)
    {
        if (global_string_variables_allocated == 0)
        {
            global_string_variables = alloc_calloc (__FILE__, __LINE__, VARIABLES_ALLOC_GRANULARITY, sizeof (VARIABLE));

            if (! global_string_variables)
            {
                return -1;
            }
        }
        else
        {
            global_string_variables = alloc_realloc (__FILE__, __LINE__, global_string_variables,
                                            (global_string_variables_allocated + VARIABLES_ALLOC_GRANULARITY) * sizeof (VARIABLE));

            if (! global_string_variables)
            {
                return -1;
            }

            memset (global_string_variables + global_string_variables_allocated, 0, VARIABLES_ALLOC_GRANULARITY * sizeof (VARIABLE));
        }

        global_string_variables_allocated += VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (global_string_variables[global_string_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    global_string_variables[global_string_variables_used].line          = line;
    global_string_variables[global_string_variables_used].used_cnt      = 0;

    rtc = global_string_variables_used;
    global_string_variables_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_global_string_variables - free global string variables
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
free_global_string_variables (void)
{
    alloc_free (__FILE__, __LINE__, global_string_variables);

    global_string_variables            = 0;
    global_string_variables_used       = 0;
    global_string_variables_allocated  = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate data for a global string array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_global_string_array_variable (unsigned char * name, int arraysize, int line)
{
    int     rtc;

    if (global_string_array_variables_used == global_string_array_variables_allocated)
    {
        if (global_string_array_variables_allocated == 0)
        {
            global_string_array_variables = alloc_calloc (__FILE__, __LINE__, ARRAY_VARIABLES_ALLOC_GRANULARITY, sizeof (ARRAY_VARIABLE));

            if (! global_string_array_variables)
            {
                return -1;
            }
        }
        else
        {
            global_string_array_variables = alloc_realloc (__FILE__, __LINE__, global_string_array_variables,
                                                    (global_string_array_variables_allocated + ARRAY_VARIABLES_ALLOC_GRANULARITY) * sizeof (ARRAY_VARIABLE));

            if (! global_string_array_variables)
            {
                return -1;
            }

            memset (global_string_array_variables + global_string_array_variables_allocated, 0, ARRAY_VARIABLES_ALLOC_GRANULARITY * sizeof (ARRAY_VARIABLE));
        }

        global_string_array_variables_allocated += ARRAY_VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (global_string_array_variables[global_string_array_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    global_string_array_variables[global_string_array_variables_used].line          = line;
    global_string_array_variables[global_string_array_variables_used].arraysize     = arraysize;
    global_string_array_variables[global_string_array_variables_used].used_cnt      = 0;

    rtc = global_string_array_variables_used;
    global_string_array_variables_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_global_string_array_variables - free global array variables
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
free_global_string_array_variables (void)
{
    alloc_free (__FILE__, __LINE__, global_string_array_variables);

    global_string_array_variables            = 0;
    global_string_array_variables_used       = 0;
    global_string_array_variables_allocated  = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate data for a global string variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_const_string_variable (unsigned char * name, int line)
{
    int     rtc;

    if (const_string_variables_used == const_string_variables_allocated)
    {
        if (const_string_variables_allocated == 0)
        {
            const_string_variables = alloc_calloc (__FILE__, __LINE__, VARIABLES_ALLOC_GRANULARITY, sizeof (VARIABLE));

            if (! const_string_variables)
            {
                return -1;
            }
        }
        else
        {
            const_string_variables = alloc_realloc (__FILE__, __LINE__, const_string_variables,
                                            (const_string_variables_allocated + VARIABLES_ALLOC_GRANULARITY) * sizeof (VARIABLE));

            if (! const_string_variables)
            {
                return -1;
            }

            memset (const_string_variables + const_string_variables_allocated, 0, VARIABLES_ALLOC_GRANULARITY * sizeof (VARIABLE));
        }

        const_string_variables_allocated += VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (const_string_variables[const_string_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    const_string_variables[const_string_variables_used].line          = line;
    const_string_variables[const_string_variables_used].used_cnt      = 0;

    rtc = const_string_variables_used;
    const_string_variables_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_const_string_variables - free sonst string variables
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
free_const_string_variables (void)
{
    alloc_free (__FILE__, __LINE__, const_string_variables);

    const_string_variables            = 0;
    const_string_variables_used       = 0;
    const_string_variables_allocated  = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a local integer variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_local_int_variable (FUNCTION * funcp, unsigned char * name)
{
    int     idx;

    for (idx = 0; idx < funcp->local_int_variables_used; idx++)
    {
        if (! ustrncmp (funcp->local_int_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a local integer array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_local_int_array_variable (FUNCTION * funcp, unsigned char * name)
{
    int     idx;

    for (idx = 0; idx < funcp->local_int_array_variables_used; idx++)
    {
        if (! ustrncmp (funcp->local_int_array_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate local integer variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_local_int_variable (FUNCTION * funcp, unsigned char * name, int line)
{
    int     rtc;

    if (funcp->local_int_variables_used == funcp->local_int_variables_allocated)
    {
        if (funcp->local_int_variables_allocated == 0)
        {
            funcp->local_int_variables = alloc_calloc (__FILE__, __LINE__, LOCAL_VARIABLES_ALLOC_GRANULARITY, sizeof (VARIABLE));

            if (! funcp->local_int_variables)
            {
                return -1;
            }
        }
        else
        {
            funcp->local_int_variables = alloc_realloc (__FILE__, __LINE__, funcp->local_int_variables,
                                                    (funcp->local_int_variables_allocated + LOCAL_VARIABLES_ALLOC_GRANULARITY) * sizeof (VARIABLE));

            if (! funcp->local_int_variables)
            {
                return -1;
            }

            memset (funcp->local_int_variables + funcp->local_int_variables_allocated, 0, LOCAL_VARIABLES_ALLOC_GRANULARITY * sizeof (VARIABLE));
        }

        funcp->local_int_variables_allocated += LOCAL_VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (funcp->local_int_variables[funcp->local_int_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    funcp->local_int_variables[funcp->local_int_variables_used].line            = line;
    funcp->local_int_variables[funcp->local_int_variables_used].used_cnt        = 0;

    rtc = funcp->local_int_variables_used;
    funcp->local_int_variables_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate local integer array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_local_int_array_variable (FUNCTION * funcp, unsigned char * name, int arraysize, int line)
{
    int     rtc;

    if (funcp->local_int_array_variables_used == funcp->local_int_array_variables_allocated)
    {
        if (funcp->local_int_array_variables_allocated == 0)
        {
            funcp->local_int_array_variables = alloc_calloc (__FILE__, __LINE__, LOCAL_ARRAY_VARIABLES_ALLOC_GRANULARITY, sizeof (ARRAY_VARIABLE));

            if (! funcp->local_int_array_variables)
            {
                return -1;
            }
        }
        else
        {
            funcp->local_int_array_variables = alloc_realloc (__FILE__, __LINE__, funcp->local_int_array_variables,
                                                        (funcp->local_int_array_variables_allocated + LOCAL_ARRAY_VARIABLES_ALLOC_GRANULARITY) * sizeof (ARRAY_VARIABLE));

            if (! funcp->local_int_array_variables)
            {
                return -1;
            }

            memset (funcp->local_int_array_variables + funcp->local_int_array_variables_allocated, 0, LOCAL_ARRAY_VARIABLES_ALLOC_GRANULARITY * sizeof (ARRAY_VARIABLE));
        }

        funcp->local_int_array_variables_allocated += LOCAL_ARRAY_VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (funcp->local_int_array_variables[funcp->local_int_array_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    funcp->local_int_array_variables[funcp->local_int_array_variables_used].line            = line;
    funcp->local_int_array_variables[funcp->local_int_array_variables_used].arraysize       = arraysize;
    funcp->local_int_array_variables[funcp->local_int_array_variables_used].used_cnt        = 0;

    rtc = funcp->local_int_array_variables_used;
    funcp->local_int_array_variables_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a local byte variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_local_byte_variable (FUNCTION * funcp, unsigned char * name)
{
    int     idx;

    for (idx = 0; idx < funcp->local_byte_variables_used; idx++)
    {
        if (! ustrncmp (funcp->local_byte_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a local integer array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_local_byte_array_variable (FUNCTION * funcp, unsigned char * name)
{
    int     idx;

    for (idx = 0; idx < funcp->local_byte_array_variables_used; idx++)
    {
        if (! ustrncmp (funcp->local_byte_array_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate local integer variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_local_byte_variable (FUNCTION * funcp, unsigned char * name, int line)
{
    int     rtc;

    if (funcp->local_byte_variables_used == funcp->local_byte_variables_allocated)
    {
        if (funcp->local_byte_variables_allocated == 0)
        {
            funcp->local_byte_variables = alloc_calloc (__FILE__, __LINE__, LOCAL_VARIABLES_ALLOC_GRANULARITY, sizeof (VARIABLE));

            if (! funcp->local_byte_variables)
            {
                return -1;
            }
        }
        else
        {
            funcp->local_byte_variables = alloc_realloc (__FILE__, __LINE__, funcp->local_byte_variables,
                                                (funcp->local_byte_variables_allocated + LOCAL_VARIABLES_ALLOC_GRANULARITY) * sizeof (VARIABLE));

            if (! funcp->local_byte_variables)
            {
                return -1;
            }

            memset (funcp->local_byte_variables + funcp->local_byte_variables_allocated, 0, LOCAL_VARIABLES_ALLOC_GRANULARITY * sizeof (VARIABLE));
        }

        funcp->local_byte_variables_allocated += LOCAL_VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (funcp->local_byte_variables[funcp->local_byte_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    funcp->local_byte_variables[funcp->local_byte_variables_used].line            = line;
    funcp->local_byte_variables[funcp->local_byte_variables_used].used_cnt        = 0;

    rtc = funcp->local_byte_variables_used;
    funcp->local_byte_variables_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate local byte array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_local_byte_array_variable (FUNCTION * funcp, unsigned char * name, int arraysize, int line)
{
    int     rtc;

    if (funcp->local_byte_array_variables_used == funcp->local_byte_array_variables_allocated)
    {
        if (funcp->local_byte_array_variables_allocated == 0)
        {
            funcp->local_byte_array_variables = alloc_calloc (__FILE__, __LINE__, LOCAL_ARRAY_VARIABLES_ALLOC_GRANULARITY, sizeof (ARRAY_VARIABLE));

            if (! funcp->local_byte_array_variables)
            {
                return -1;
            }
        }
        else
        {
            funcp->local_byte_array_variables = alloc_realloc (__FILE__, __LINE__, funcp->local_byte_array_variables,
                                                        (funcp->local_byte_array_variables_allocated + LOCAL_ARRAY_VARIABLES_ALLOC_GRANULARITY) * sizeof (ARRAY_VARIABLE));

            if (! funcp->local_byte_array_variables)
            {
                return -1;
            }

            memset (funcp->local_byte_array_variables + funcp->local_byte_array_variables_allocated, 0, LOCAL_ARRAY_VARIABLES_ALLOC_GRANULARITY * sizeof (ARRAY_VARIABLE));
        }

        funcp->local_byte_array_variables_allocated += LOCAL_ARRAY_VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (funcp->local_byte_array_variables[funcp->local_byte_array_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    funcp->local_byte_array_variables[funcp->local_byte_array_variables_used].line            = line;
    funcp->local_byte_array_variables[funcp->local_byte_array_variables_used].arraysize       = arraysize;
    funcp->local_byte_array_variables[funcp->local_byte_array_variables_used].used_cnt        = 0;

    rtc = funcp->local_byte_array_variables_used;
    funcp->local_byte_array_variables_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a local string variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_local_string_variable (FUNCTION * funcp, unsigned char * name)
{
    int     idx;

    for (idx = 0; idx < funcp->local_string_variables_used; idx++)
    {
        if (! ustrncmp (funcp->local_string_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * find a local string array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
find_local_string_array_variable (FUNCTION * funcp, unsigned char * name)
{
    int     idx;

    for (idx = 0; idx < funcp->local_string_array_variables_used; idx++)
    {
        if (! ustrncmp (funcp->local_string_array_variables[idx].name, name, MAX_VARIABLE_NAME_LEN))
        {
            return idx;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate data for a local string variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_local_string_variable (FUNCTION * funcp, unsigned char * name, int line)
{
    int     rtc;

    if (funcp->local_string_variables_used == funcp->local_string_variables_allocated)
    {
        if (funcp->local_string_variables_allocated == 0)
        {
            funcp->local_string_variables = alloc_calloc (__FILE__, __LINE__, LOCAL_VARIABLES_ALLOC_GRANULARITY, sizeof (VARIABLE));

            if (! funcp->local_string_variables)
            {
                return -1;
            }
        }
        else
        {
            funcp->local_string_variables = alloc_realloc (__FILE__, __LINE__, funcp->local_string_variables,
                                                        (funcp->local_string_variables_allocated + LOCAL_VARIABLES_ALLOC_GRANULARITY) * sizeof (VARIABLE));

            if (! funcp->local_string_variables)
            {
                return -1;
            }

            memset (funcp->local_string_variables + funcp->local_string_variables_allocated, 0, LOCAL_VARIABLES_ALLOC_GRANULARITY * sizeof (VARIABLE));
        }

        funcp->local_string_variables_allocated += LOCAL_VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (funcp->local_string_variables[funcp->local_string_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    funcp->local_string_variables[funcp->local_string_variables_used].line      = line;
    funcp->local_string_variables[funcp->local_string_variables_used].used_cnt  = 0;

    rtc = funcp->local_string_variables_used;
    funcp->local_string_variables_used++;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * allocate data for a local string array variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
new_local_string_array_variable (FUNCTION * funcp, unsigned char * name, int arraysize, int line)
{
    int     rtc;

    if (funcp->local_string_array_variables_used == funcp->local_string_array_variables_allocated)
    {
        if (funcp->local_string_array_variables_allocated == 0)
        {
            funcp->local_string_array_variables = alloc_calloc (__FILE__, __LINE__, LOCAL_ARRAY_VARIABLES_ALLOC_GRANULARITY, sizeof (ARRAY_VARIABLE));

            if (! funcp->local_string_array_variables)
            {
                return -1;
            }
        }
        else
        {
            funcp->local_string_array_variables = alloc_realloc (__FILE__, __LINE__, funcp->local_string_array_variables,
                                                    (funcp->local_string_array_variables_allocated + LOCAL_ARRAY_VARIABLES_ALLOC_GRANULARITY) * sizeof (ARRAY_VARIABLE));

            if (! funcp->local_string_array_variables)
            {
                return -1;
            }

            memset (funcp->local_string_array_variables + funcp->local_string_array_variables_allocated, 0,
                    LOCAL_ARRAY_VARIABLES_ALLOC_GRANULARITY * sizeof (ARRAY_VARIABLE));
        }

        funcp->local_string_array_variables_allocated += LOCAL_ARRAY_VARIABLES_ALLOC_GRANULARITY;
    }

    ustrncpy (funcp->local_string_array_variables[funcp->local_string_array_variables_used].name, name, MAX_VARIABLE_NAME_LEN);
    funcp->local_string_array_variables[funcp->local_string_array_variables_used].line      = line;
    funcp->local_string_array_variables[funcp->local_string_array_variables_used].arraysize = arraysize;
    funcp->local_string_array_variables[funcp->local_string_array_variables_used].used_cnt  = 0;

    rtc = funcp->local_string_array_variables_used;
    funcp->local_string_array_variables_used++;

    return rtc;
}

typedef enum
{
    NO_FLAG,
    IS_FUNCTION_DEFINITION_FLAG,
    WAITING_FOR_COMPARE_OPERATOR_FLAG,
    WAITING_FOR_TO_OPERATOR_FLAG,
    WAITING_FOR_STEP_OPERATOR_FLAG
} HANDLE_EXPRESSION_FLAG;

static int last_undefined_function_idx = -1;
static int last_void_function_idx;
static int last_void_function_type;

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * define_function - define function
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
define_function (int line, unsigned char * kw, unsigned char * np, int function_type, unsigned char ** nextpp)
{
    unsigned char   varname[2 * MAX_VARIABLE_NAME_LEN + 1];
    unsigned char * nextp = *nextpp;
    int             varidx;
    int             fidx;
    int             bracket_cnt = 0;
    int             argc = 0;
    int             rtc = EXPRESSION_NO_ERROR;

    if ((fidx = find_function (kw)) >= 0)
    {
        fprintf (stderr, "error line %d: function '%s' already defined in line %d.\n", line, kw, functions[fidx].line);
        rtc = EXPRESSION_ERROR;
        return rtc;
    }

    current_function_idx = new_function (kw, line, function_type, statements_used);

    sprintf ((char *) varname, "function.%s", kw);
    varidx = new_global_int_variable (varname, line);
    global_int_variables[varidx].v.int_value = current_function_idx;

    nextp++;                                    // skip '('
    nextp = skip_blanks (nextp);

    if (* nextp == ')')                         // empty argument list
    {
        unsigned char * pp = skip_blanks (nextp + 1);

        if (*pp)
        {
            if (*pp != '/' || *(pp +1) != '/')
            {
                fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, pp, __LINE__);
                rtc = EXPRESSION_ERROR;
                return rtc;
            }
        }
    }

    while (*np)
    {
        if (*np == '(')
        {
            bracket_cnt++;
        }

        if (bracket_cnt == 0)
        {
            if (*np == ',' || *np == ')')
            {
                unsigned char   kw2[MAX_VARIABLE_NAME_LEN];
                int             save_ch = *np;
                int             t2;
                unsigned char * p2;
                unsigned char * pp2;

                *np = '\0';
                p2 = nextp;

                t2 = check_keyword (kw2, line, p2, &pp2, FALSE);

                if (t2 == KEYWORD_IS_EMPTY)
                {
                    functions[current_function_idx].argc = argc;
                    continue;
                }

                if (t2 != KEYWORD_IS_IDENTIFIER)
                {
                    fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                    rtc = EXPRESSION_ERROR;
                    break;
                }

                p2 = pp2;

                if (! ustrcmp (kw2, "int"))
                {
                    int             varidx;

                    if (check_keyword (kw2, line, p2, &pp2, FALSE) != KEYWORD_IS_IDENTIFIER)
                    {
                        fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                        rtc = EXPRESSION_ERROR;
                        break;
                    }

                    varidx = new_local_int_variable (functions + current_function_idx, kw2, line);
                    new_arg (functions + current_function_idx, varidx, ARGUMENT_TYPE_INT);
                }
                else if (! ustrcmp (kw2, "byte"))
                {
                    int             varidx;

                    if (check_keyword (kw2, line, p2, &pp2, FALSE) != KEYWORD_IS_IDENTIFIER)
                    {
                        fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                        rtc = EXPRESSION_ERROR;
                        break;
                    }

                    varidx = new_local_byte_variable (functions + current_function_idx, kw2, line);
                    new_arg (functions + current_function_idx, varidx, ARGUMENT_TYPE_BYTE);
                }
                else if (! ustrcmp (kw2, "string"))
                {
                    int     varidx;

                    if (check_keyword (kw2, line, p2, &pp2, FALSE) != KEYWORD_IS_IDENTIFIER)
                    {
                        fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                        rtc = EXPRESSION_ERROR;
                        break;
                    }

                    varidx = new_local_string_variable (functions + current_function_idx, kw2, line);
                    new_arg (functions + current_function_idx, varidx, ARGUMENT_TYPE_STRING);
                }
                else
                {
                    fprintf (stderr, "error line %d: unknown argument type.\n", line);
                    rtc = EXPRESSION_ERROR;
                    break;
                }

                argc++;
                *np = save_ch;

                nextp = np + 1;
                nextp = skip_blanks (nextp);
            }

            if (*np == ')')
            {
                break;
            }
        }
        else if (*np == ')')
        {
            bracket_cnt--;
        }

        np++;
    }

    if (! *np && bracket_cnt != 0)
    {
        fprintf (stderr, "error line %d: no matching ')' found.\n", line);
        rtc = EXPRESSION_ERROR;
        return rtc;
    }

    functions[current_function_idx].argc = argc;

    *nextpp = nextp;
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_args - free arguments
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
free_args (char * file, int line, int argc, EXPRESSION_LIST ** argvp)
{
    int i;

    for (i = 0; i < argc; i++)
    {
        free_expression_list (file, line, argvp[i]);
    }

    alloc_free (file, line, argvp);
}

static HANDLE_EXPRESSION_RTC handle_expression (int, EXPRESSION_LIST *, unsigned char *, HANDLE_EXPRESSION_FLAG, unsigned char **);

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * handle_arguments - handle arguments
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
handle_arguments (int line, EXPRESSION_LIST * expr, int * expr_idxp, unsigned char * kw, unsigned char * np, int * neg, int * inv, unsigned char ** nextpp)
{
    EXPRESSION_LIST **  argvp;
    unsigned char *     nextp = *nextpp;
    int                 argc = 0;
    int                 fipslot;
    int                 in_string = 0;
    int                 bracket_cnt = 0;
    int                 idx;
    int                 expr_idx = *expr_idxp;
    int                 siz = sizeof (function_list) / sizeof (function_list[0]);
    int                 rtc = EXPRESSION_NO_ERROR;

    for (idx = 0; idx < siz; idx++)
    {
        if (! ustrcmp (kw, function_list[idx].name))
        {
            break;
        }
    }

    if (idx == siz)
    {
        for (idx = 0; idx < functions_used; idx++)
        {
            if (! ustrcmp (kw, functions[idx].name))
            {
                functions[idx].used_cnt++;
                break;
            }
        }

        if (idx == functions_used)
        {
            idx = new_undefined_function (kw, line);
            last_undefined_function_idx = idx;
            expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_UNDEFINED_FUNCTION;
        }
        else
        {
            if (functions[idx].return_type == FUNCTION_TYPE_VOID)
            {
                last_void_function_idx = idx;
                last_void_function_type = EXPRESSION_CONTENT_TYPE_EXTERN_FUNCTION;
                rtc = FUNCTION_RETURNING_VOID;
            }

            expr->ec[expr_idx].type     = EXPRESSION_CONTENT_TYPE_EXTERN_FUNCTION;
        }
    }
    else
    {
        if (function_list[idx].return_type == FUNCTION_TYPE_VOID)
        {
            last_void_function_idx = idx;
            last_void_function_type = EXPRESSION_CONTENT_TYPE_INTERN_FUNCTION;
            rtc = FUNCTION_RETURNING_VOID;
        }
        expr->ec[expr_idx].type     = EXPRESSION_CONTENT_TYPE_INTERN_FUNCTION;
    }

    if (*neg)                                                               // -kw -> (0-kw)
    {
        *neg = 0;

        if (expr_idx >= expr->allocated - 2)                                // shift expression contents right
        {
            resize_expression_list (__FILE__, __LINE__, expr);
        }

        expr->ec[expr_idx + 1].type = expr->ec[expr_idx].type;
        expr->ec[expr_idx + 1].obr  = expr->ec[expr_idx].obr;
        expr->ec[expr_idx + 1].cbr  = expr->ec[expr_idx].cbr + 1;
        expr->ec[expr_idx + 1].op   = expr->ec[expr_idx].op;

        expr->ec[expr_idx].type     = EXPRESSION_CONTENT_TYPE_INT_CONSTANT;
        expr->ec[expr_idx].value    = 0;
        expr->ec[expr_idx].obr      = 1;

        if (! *nextp)
        {
            expr->ec[expr_idx].cbr  = 1;
        }
        else
        {
            expr->ec[expr_idx].cbr  = 0;
        }
        expr->ec[expr_idx].op       = '-';
        expr_idx++;
    }
    else if (*inv)                                                          // ~kw -> (0~kw)
    {
        *inv = 0;

        if (expr_idx >= expr->allocated - 2)                                // shift expression contents right
        {
            resize_expression_list (__FILE__, __LINE__, expr);
        }

        expr->ec[expr_idx + 1].type = expr->ec[expr_idx].type;
        expr->ec[expr_idx + 1].obr  = expr->ec[expr_idx].obr;
        expr->ec[expr_idx + 1].cbr  = expr->ec[expr_idx].cbr + 1;
        expr->ec[expr_idx + 1].op   = expr->ec[expr_idx].op;

        expr->ec[expr_idx].type     = EXPRESSION_CONTENT_TYPE_INT_CONSTANT;
        expr->ec[expr_idx].value    = 0;
        expr->ec[expr_idx].obr      = 1;

        if (! *nextp)
        {
            expr->ec[expr_idx].cbr  = 1;
        }
        else
        {
            expr->ec[expr_idx].cbr  = 0;
        }
        expr->ec[expr_idx].op       = '~';
        expr_idx++;
    }

    expr->ec[expr_idx].value    = idx;
    argvp = alloc_calloc (__FILE__, __LINE__, 32, sizeof (EXPRESSION_LIST *));
    nextp++;

    np = skip_blanks (np);
    nextp = skip_blanks (nextp);

    while (*np)
    {
        if (*np == '(')
        {
            bracket_cnt++;
        }
        else if (*np == '"')
        {
            in_string = in_string ? 0 : 1;
        }

        if (bracket_cnt == 0)
        {
            if (! in_string && (*np == ',' || *np == ')'))
            {
                EXPRESSION_LIST *   sub_expr;
                int                 ll;

                sub_expr = new_expression_list (__FILE__, __LINE__);

                ll = np - nextp;

                if (ll > 0)
                {
                    int save_last_undefined_function_idx;
                    int             r;
                    unsigned char * subs;

                    subs = alloc_malloc (__FILE__, __LINE__, ll + 1);
                    ustrncpy (subs, nextp, ll);
                    *(subs + ll) = '\0';

                    save_last_undefined_function_idx = last_undefined_function_idx;
                    last_undefined_function_idx = -1;

                    if ((r = handle_expression (line, sub_expr, subs, 0, (unsigned char **) NULL)) == EXPRESSION_ERROR)
                    {
                        alloc_free (__FILE__, __LINE__, subs);
                        free_expression_list (__FILE__, __LINE__, sub_expr);

                        while (argc > 0)
                        {
                            argc--;
                            free_expression_list (__FILE__, __LINE__, argvp[argc]);
                        }

                        rtc = EXPRESSION_ERROR;
                        break;
                    }

                    if (last_undefined_function_idx >= 0)
                    {
                        undefined_functions[last_undefined_function_idx].needs_return_value = 1;
                    }
                    else
                    {
                        last_undefined_function_idx = save_last_undefined_function_idx;
                    }

                    if (r == FUNCTION_RETURNING_VOID)
                    {
                        if (last_void_function_type == EXPRESSION_CONTENT_TYPE_INTERN_FUNCTION)
                        {
                            fprintf (stderr, "error line %d: function '%s' returns void.\n", line,
                                    function_list[last_void_function_idx].name);
                        }
                        else // if (last_void_function_type == EXPRESSION_CONTENT_TYPE_EXTERN_FUNCTION)
                        {
                            fprintf (stderr, "error line %d: function '%s' defined in line %d returns void.\n", line,
                                            functions[last_void_function_idx].name, functions[last_void_function_idx].line);
                        }

                        alloc_free (__FILE__, __LINE__, subs);
                        free_expression_list (__FILE__, __LINE__, sub_expr);

                        while (argc > 0)
                        {
                            argc--;
                            free_expression_list (__FILE__, __LINE__, argvp[argc]);
                        }

                        rtc = EXPRESSION_ERROR;
                        break;
                    }

                    alloc_free (__FILE__, __LINE__, subs);
                    argvp[argc++] = sub_expr;
                }
                else if (*np == ',')                                            // empty expression preceding ','
                {
                    free_expression_list (__FILE__, __LINE__, sub_expr);

                    while (argc > 0)
                    {
                        argc--;
                        free_expression_list (__FILE__, __LINE__, argvp[argc]);
                    }

                    fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                    rtc = EXPRESSION_ERROR;
                    break;
                }
                else
                {
                    free_expression_list (__FILE__, __LINE__, sub_expr);
                }

                nextp = np + 1;
            }

            if (*np == ')')
            {
                break;
            }
        }
        else if (*np == ')')
        {
            bracket_cnt--;
        }

        np++;
    }

    if (rtc == EXPRESSION_ERROR)
    {
        alloc_free (__FILE__, __LINE__, argvp);
        return rtc;
    }

    if (! *np && bracket_cnt != 0)
    {
        free_args (__FILE__, __LINE__, argc, argvp);
        fprintf (stderr, "error line %d: no matching ')' found.\n", line);
        rtc = EXPRESSION_ERROR;
        return rtc;
    }

    if (expr->ec[expr_idx].type == EXPRESSION_CONTENT_TYPE_INTERN_FUNCTION)
    {
        if (argc < function_list[idx].min_args)
        {
            free_args (__FILE__, __LINE__, argc, argvp);
            fprintf (stderr, "error line %d: missing arguments for function '%s'.\n", line, function_list[idx].name);
            rtc = EXPRESSION_ERROR;
            return rtc;
        }
        else if (argc > function_list[idx].max_args)
        {
            free_args (__FILE__, __LINE__, argc, argvp);
            fprintf (stderr, "error line %d: too many arguments for function '%s'.\n", line, function_list[idx].name);
            rtc = EXPRESSION_ERROR;
            return rtc;
        }
    }
    else if (expr->ec[expr_idx].type == EXPRESSION_CONTENT_TYPE_EXTERN_FUNCTION)
    {
        if (functions[idx].argc != argc)
        {
            free_args (__FILE__, __LINE__, argc, argvp);
            fprintf (stderr, "error line %d: number of arguments wrong for call of function '%s' defined in line %d: got %d, expected %d.\n",
                     line, functions[idx].name, functions[idx].line, argc, functions[idx].argc);
            rtc = EXPRESSION_ERROR;
            return rtc;
        }
    }
    else // if (expr->ec[expr_idx].type == EXPRESSION_CONTENT_TYPE_UNDEFINED_FUNCTION)
    {
        undefined_functions[expr->ec[expr_idx].value].argc = argc;      // do nothing else here, check it in check_undefined_functions();
    }

    fipslot = new_fipslot (idx, argc, argvp);

    expr->ec[expr_idx].fipslot = fipslot;

    if (expr_idx >= expr->allocated - 1)
    {
        resize_expression_list (__FILE__, __LINE__, expr);
    }

    expr_idx++;
    expr->ec[expr_idx].obr      = 0;
    expr->ec[expr_idx].cbr      = 0;
    expr->ec[expr_idx].op       = 0;

    *nextpp = nextp;
    *expr_idxp = expr_idx;
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * handle an expression
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static HANDLE_EXPRESSION_RTC
handle_expression (int line, EXPRESSION_LIST * expr, unsigned char * p, HANDLE_EXPRESSION_FLAG flag, unsigned char ** restp)
{
    unsigned char       kw[MAX_KEYWORD_LEN + 1];
    unsigned char *     nextp;
    int                 type;
    int                 open_brackets = 0;
//  int                 open_square_brackets = 0;
    int                 last_keyword_was_operator = -1;
    int                 negate_operand = 0;
    int                 invert_operand = 0;
    int                 to_keyword_found = 0;
    int                 step_keyword_found = 0;
    int                 expr_idx = 0;
    int                 varidx;
    int                 rtc = EXPRESSION_NO_ERROR;

    expr->ec[expr_idx].obr      = 0;
    expr->ec[expr_idx].cbr      = 0;
    expr->ec[expr_idx].op       = 0;

    while (p && *p)
    {
        type = check_keyword (kw, line, p, &nextp, FALSE);

        if (type < 0)
        {
            fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
            rtc = EXPRESSION_ERROR;
            break;
        }
        else if (type == KEYWORD_IS_EMPTY)
        {
            break;
        }
        else if (type == KEYWORD_IS_INT)
        {
            if (last_keyword_was_operator == 0)
            {
                fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                rtc = EXPRESSION_ERROR;
                break;
            }

            last_keyword_was_operator   = 0;

            expr->ec[expr_idx].type     = EXPRESSION_CONTENT_TYPE_INT_CONSTANT;
            expr->ec[expr_idx].value    = uatoi (kw);

            if (negate_operand)
            {
                negate_operand = 0;

                if (expr->ec[expr_idx].obr == 0)                                            // simple integer
                {
                    expr->ec[expr_idx].value = -expr->ec[expr_idx].value;                   // negate it
                }
                else
                {
                    if (expr_idx >= expr->allocated - 2)                                    // there is a least one open bracket: shift expression contents right
                    {
                        resize_expression_list (__FILE__, __LINE__, expr);
                    }

                    expr->ec[expr_idx + 1].value    = expr->ec[expr_idx].value;
                    expr->ec[expr_idx + 1].type     = expr->ec[expr_idx].type;
                    expr->ec[expr_idx + 1].obr      = expr->ec[expr_idx].obr;
                    expr->ec[expr_idx + 1].cbr      = expr->ec[expr_idx].cbr + 1;
                    expr->ec[expr_idx + 1].op       = expr->ec[expr_idx].op;

                    expr->ec[expr_idx].type         = EXPRESSION_CONTENT_TYPE_INT_CONSTANT;
                    expr->ec[expr_idx].value        = 0;
                    expr->ec[expr_idx].obr          = 1;

                    if (! *nextp)
                    {
                        expr->ec[expr_idx].cbr      = 1;
                    }
                    else
                    {
                        expr->ec[expr_idx].cbr      = 0;
                    }
                    expr->ec[expr_idx].op           = '-';
                    expr_idx++;
                }
            }
            else if (invert_operand)
            {
                invert_operand = 0;

                if (expr->ec[expr_idx].obr == 0)                                            // simple integer
                {
                    expr->ec[expr_idx].value = ~expr->ec[expr_idx].value;                   // invert it
                }
                else
                {
                    if (expr_idx >= expr->allocated - 2)                                    // there is a least one open bracket: shift expression contents right
                    {
                        resize_expression_list (__FILE__, __LINE__, expr);
                    }

                    expr->ec[expr_idx + 1].value    = expr->ec[expr_idx].value;
                    expr->ec[expr_idx + 1].type     = expr->ec[expr_idx].type;
                    expr->ec[expr_idx + 1].obr      = expr->ec[expr_idx].obr;
                    expr->ec[expr_idx + 1].cbr      = expr->ec[expr_idx].cbr + 1;
                    expr->ec[expr_idx + 1].op       = expr->ec[expr_idx].op;

                    expr->ec[expr_idx].type         = EXPRESSION_CONTENT_TYPE_INT_CONSTANT;
                    expr->ec[expr_idx].value        = 0;
                    expr->ec[expr_idx].obr          = 1;

                    if (! *nextp)
                    {
                        expr->ec[expr_idx].cbr      = 1;
                    }
                    else
                    {
                        expr->ec[expr_idx].cbr      = 0;
                    }
                    expr->ec[expr_idx].op           = '~';
                    expr_idx++;
                }
            }

            if (expr_idx >= expr->allocated - 1)
            {
                resize_expression_list (__FILE__, __LINE__, expr);
            }

            expr_idx++;
            expr->ec[expr_idx].obr      = 0;
            expr->ec[expr_idx].cbr      = 0;
            expr->ec[expr_idx].op       = 0;
        }
        else if (type == KEYWORD_IS_STRING)
        {
            if (last_keyword_was_operator == 0)
            {
                fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                rtc = EXPRESSION_ERROR;
                break;
            }

            expr->ec[expr_idx].type     = EXPRESSION_CONTENT_TYPE_STRING_CONSTANT;
            expr->ec[expr_idx].value    = new_string_constant (kw);

            if (expr_idx >= expr->allocated - 1)
            {
                resize_expression_list (__FILE__, __LINE__, expr);
            }

            expr_idx++;
            expr->ec[expr_idx].obr      = 0;
            expr->ec[expr_idx].cbr      = 0;
            expr->ec[expr_idx].op       = 0;

            last_keyword_was_operator   = 0;

        }
        else if (type == KEYWORD_IS_IDENTIFIER)
        {
            unsigned char   k[MAX_VARIABLE_NAME_LEN + 1];
            int             function_type = FUNCTION_TYPE_INT;
            int             t;
            unsigned char * np;

            if (last_keyword_was_operator == 0)
            {
                if (flag == WAITING_FOR_TO_OPERATOR_FLAG && ! ustrcmp (kw, "to"))
                {
                    to_keyword_found = 1;
                    break;
                }
                else if (flag == WAITING_FOR_STEP_OPERATOR_FLAG && ! ustrcmp (kw, "step"))
                {
                    step_keyword_found = 1;
                    break;
                }
                else
                {
                    fprintf (stderr, "error line %d: syntax error '%s' (%d).\n", line, kw, __LINE__);
                    rtc = EXPRESSION_ERROR;
                    break;
                }
            }

            last_keyword_was_operator   = 0;

            if (flag == IS_FUNCTION_DEFINITION_FLAG)
            {
                if (! ustrcmp (kw, "void"))
                {
                    function_type = FUNCTION_TYPE_VOID;
                }
                else if (! ustrcmp (kw, "int"))
                {
                    function_type = FUNCTION_TYPE_INT;
                }
                else if (! ustrcmp (kw, "byte"))
                {
                    function_type = FUNCTION_TYPE_BYTE;
                }
                else if (! ustrcmp (kw, "string"))
                {
                    function_type = FUNCTION_TYPE_STRING;
                }
                else
                {
                    fprintf (stderr, "error line %d: wrong function type: '%s'.\n", line, kw);
                    rtc = EXPRESSION_ERROR;
                    break;
                }

                t = check_keyword (kw, line, nextp, &np, FALSE);
                nextp = np;
            }

            t = check_keyword (k, line, nextp, &np, FALSE);

            if (t == KEYWORD_IS_OPEN_BRACKET)
            {
                if (!*np)
                {
                    fprintf (stderr, "error line %d: missing closing bracket.\n", line);
                    rtc = EXPRESSION_ERROR;
                    break;
                }

                if (flag == IS_FUNCTION_DEFINITION_FLAG)
                {
                    rtc = define_function (line, kw, np, function_type, &nextp);
                }
                else
                {
                    rtc = handle_arguments (line, expr, &expr_idx, kw, np, &negate_operand, &invert_operand, &nextp);
                }

                if (rtc == EXPRESSION_ERROR)
                {
                    // printf ("got error: %s %d\n", __FILE__, __LINE__);
                    break;
                }
            }
            else
            {
                int     arraysize = 0;
                int     pslot = -1;

                // printf ("line %d: variable: '%s'\n", line, kw);

                if (flag == IS_FUNCTION_DEFINITION_FLAG)
                {
                    fprintf (stderr, "error line %d: missing arguments of function '%s'.\n", line, kw);
                    rtc = EXPRESSION_ERROR;
                    break;
                }

                if (negate_operand)                                                     // -kw -> (0-kw)
                {
                    negate_operand = 0;

                    if (expr_idx >= expr->allocated - 2)                                // shift expression contents right
                    {
                        resize_expression_list (__FILE__, __LINE__, expr);
                    }

                    expr->ec[expr_idx + 1].obr = expr->ec[expr_idx].obr;
                    expr->ec[expr_idx + 1].cbr = expr->ec[expr_idx].cbr + 1;
                    expr->ec[expr_idx + 1].op  = expr->ec[expr_idx].op;

                    expr->ec[expr_idx].type     = EXPRESSION_CONTENT_TYPE_INT_CONSTANT;
                    expr->ec[expr_idx].value    = 0;
                    expr->ec[expr_idx].obr      = 1;

                    if (! *nextp)
                    {
                        expr->ec[expr_idx].cbr  = 1;
                    }
                    else
                    {
                        expr->ec[expr_idx].cbr  = 0;
                    }
                    expr->ec[expr_idx].op       = '-';
                    expr_idx++;
                }
                else if (invert_operand)                                                    // ~kw -> (0~kw)
                {
                    invert_operand = 0;

                    if (expr_idx >= expr->allocated - 2)                                        // shift expression contents right
                    {
                        resize_expression_list (__FILE__, __LINE__, expr);
                    }

                    expr->ec[expr_idx + 1].obr = expr->ec[expr_idx].obr;
                    expr->ec[expr_idx + 1].cbr = expr->ec[expr_idx].cbr + 1;
                    expr->ec[expr_idx + 1].op  = expr->ec[expr_idx].op;

                    expr->ec[expr_idx].type     = EXPRESSION_CONTENT_TYPE_INT_CONSTANT;
                    expr->ec[expr_idx].value    = 0;
                    expr->ec[expr_idx].obr      = 1;

                    if (! *nextp)
                    {
                        expr->ec[expr_idx].cbr  = 1;
                    }
                    else
                    {
                        expr->ec[expr_idx].cbr  = 0;
                    }
                    expr->ec[expr_idx].op       = '~';
                    expr_idx++;
                }

                p = nextp;
                p = skip_blanks (p);

                if (*p == '[')
                {
                    EXPRESSION_LIST *   sub_expr = (EXPRESSION_LIST *) NULL;
                    POSTFIX_ELEMENT     postfix[MAX_POSTFIX_DEPTH];
                    int                 square_bracket_cnt = 1;
                    unsigned char *     ppp;
                    int                 r;

                    p++;

                    ppp = p;

                    while (*ppp)
                    {
                        if (*ppp == ']')
                        {
                            square_bracket_cnt--;

                            if (square_bracket_cnt == 0)
                            {
                                break;
                            }
                        }
                        else if (*ppp == '[')
                        {
                            square_bracket_cnt++;
                        }
                        ppp++;
                    }

                    if (! *ppp)
                    {
                        fprintf (stderr, "error line %d: no matching ']' found.\n", line);
                        rtc = EXPRESSION_ERROR;
                    }

                    *ppp = '\0';

                    sub_expr = new_expression_list (__FILE__, __LINE__);

                    if ((r = handle_expression (line, sub_expr, p, 0, (unsigned char **) NULL)) == EXPRESSION_ERROR)
                    {
                        rtc = EXPRESSION_ERROR;
                        break;
                    }

                    infix2postfix (postfix, sub_expr->ec);

                    free_expression_list (__FILE__, __LINE__, sub_expr);

                    pslot = new_postfix_slot (postfix);

                    if (pslot < 0)
                    {
                        fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                        return -1;
                    }

                    ppp++;
                    nextp = ppp;
                    p = nextp;
                }

                if ((varidx = find_local_int_variable (functions + current_function_idx, kw)) >= 0)
                {
                    functions[current_function_idx].local_int_variables[varidx].used_cnt++;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_LOCAL_INT_VARIABLE;
                }
                else if ((varidx = find_local_int_array_variable (functions + current_function_idx, kw)) >= 0)
                {
                    functions[current_function_idx].local_int_array_variables[varidx].used_cnt++;
                    arraysize = functions[current_function_idx].local_int_array_variables[varidx].arraysize;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_LOCAL_INT_ARRAY_VARIABLE;
                }
                else if ((varidx = find_local_byte_variable (functions + current_function_idx, kw)) >= 0)
                {
                    functions[current_function_idx].local_byte_variables[varidx].used_cnt++;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_LOCAL_BYTE_VARIABLE;
                }
                else if ((varidx = find_local_byte_array_variable (functions + current_function_idx, kw)) >= 0)
                {
                    functions[current_function_idx].local_byte_array_variables[varidx].used_cnt++;
                    arraysize = functions[current_function_idx].local_byte_array_variables[varidx].arraysize;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_LOCAL_BYTE_ARRAY_VARIABLE;
                }

                else if ((varidx = find_local_string_variable (functions + current_function_idx, kw)) >= 0)
                {
                    functions[current_function_idx].local_string_variables[varidx].used_cnt++;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_LOCAL_STRING_VARIABLE;
                }
                else if ((varidx = find_local_string_array_variable (functions + current_function_idx, kw)) >= 0)
                {
                    functions[current_function_idx].local_string_array_variables[varidx].used_cnt++;
                    arraysize = functions[current_function_idx].local_string_array_variables[varidx].arraysize;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_LOCAL_STRING_ARRAY_VARIABLE;
                }
                else if ((varidx = find_static_int_variable (functions + current_function_idx, kw)) >= 0)
                {
                    global_int_variables[varidx].used_cnt++;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_GLOBAL_INT_VARIABLE;
                }
                else if ((varidx = find_static_int_array_variable (functions + current_function_idx, kw)) >= 0)
                {
                    global_int_array_variables[varidx].used_cnt++;
                    arraysize = global_int_array_variables[varidx].arraysize;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_GLOBAL_INT_ARRAY_VARIABLE;
                }
                else if ((varidx = find_static_byte_variable (functions + current_function_idx, kw)) >= 0)
                {
                    global_byte_variables[varidx].used_cnt++;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_GLOBAL_BYTE_VARIABLE;
                }
                else if ((varidx = find_static_byte_array_variable (functions + current_function_idx, kw)) >= 0)
                {
                    global_byte_array_variables[varidx].used_cnt++;
                    arraysize = global_byte_array_variables[varidx].arraysize;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_GLOBAL_BYTE_ARRAY_VARIABLE;
                }
                else if ((varidx = find_static_string_variable (functions + current_function_idx, kw)) >= 0)
                {
                    global_string_variables[varidx].used_cnt++;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_GLOBAL_STRING_VARIABLE;
                }
                else if ((varidx = find_static_string_array_variable (functions + current_function_idx, kw)) >= 0)
                {
                    global_string_array_variables[varidx].used_cnt++;
                    arraysize = global_string_array_variables[varidx].arraysize;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_GLOBAL_STRING_ARRAY_VARIABLE;
                }
                else if ((varidx = find_local_const_int_variable (functions + current_function_idx, kw)) >= 0)
                {
                    const_int_variables[varidx].used_cnt++;
                    varidx = const_int_variables[varidx].v.int_value;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_INT_CONSTANT;
                }
                else if ((varidx = find_local_const_string_variable (functions + current_function_idx, kw)) >= 0)
                {
                    const_string_variables[varidx].used_cnt++;
                    varidx = new_string_constant (const_string_variables[varidx].v.str_value);
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_STRING_CONSTANT;
                }
                else if ((varidx = find_global_const_int_variable (kw)) >= 0)
                {
                    const_int_variables[varidx].used_cnt++;
                    varidx = const_int_variables[varidx].v.int_value;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_INT_CONSTANT;
                }
                else if ((varidx = find_global_const_string_variable (kw)) >= 0)
                {
                    const_string_variables[varidx].used_cnt++;
                    varidx = new_string_constant (const_string_variables[varidx].v.str_value);
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_STRING_CONSTANT;
                }
                else if ((varidx = find_global_int_variable (kw)) >= 0)
                {
                    global_int_variables[varidx].used_cnt++;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_GLOBAL_INT_VARIABLE;
                }
                else if ((varidx = find_global_int_array_variable (kw)) >= 0)
                {
                    global_int_array_variables[varidx].used_cnt++;
                    arraysize = global_int_array_variables[varidx].arraysize;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_GLOBAL_INT_ARRAY_VARIABLE;
                }
                else if ((varidx = find_global_byte_variable (kw)) >= 0)
                {
                    global_byte_variables[varidx].used_cnt++;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_GLOBAL_BYTE_VARIABLE;
                }
                else if ((varidx = find_global_byte_array_variable (kw)) >= 0)
                {
                    global_byte_array_variables[varidx].used_cnt++;
                    arraysize = global_byte_array_variables[varidx].arraysize;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_GLOBAL_BYTE_ARRAY_VARIABLE;
                }
                else if ((varidx = find_global_string_variable (kw)) >= 0)
                {
                    global_string_variables[varidx].used_cnt++;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_GLOBAL_STRING_VARIABLE;
                }
                else if ((varidx = find_global_string_array_variable (kw)) >= 0)
                {
                    global_string_array_variables[varidx].used_cnt++;
                    arraysize = global_string_array_variables[varidx].arraysize;
                    expr->ec[expr_idx].type = EXPRESSION_CONTENT_TYPE_GLOBAL_STRING_ARRAY_VARIABLE;
                }
                else
                {
                    fprintf (stderr, "error line %d: variable '%s' undefined.\n", line, kw);
                    rtc = EXPRESSION_ERROR;
                    break;
                }

                if (pslot >= 0 && arraysize == 0)
                {
                    fprintf (stderr, "error line %d: variable '%s' is not an array variable.\n", line, kw);
                    rtc = EXPRESSION_ERROR;
                    break;
                }

                if (pslot < 0 && arraysize > 0)
                {
                    if (expr->ec[expr_idx].type != EXPRESSION_CONTENT_TYPE_LOCAL_BYTE_ARRAY_VARIABLE &&             // accept pointer to byte array
                        expr->ec[expr_idx].type != EXPRESSION_CONTENT_TYPE_GLOBAL_BYTE_ARRAY_VARIABLE)
                    {
                        fprintf (stderr, "error line %d: variable '%s' is an array variable.\n", line, kw);
                        rtc = EXPRESSION_ERROR;
                        break;
                    }
                }

                expr->ec[expr_idx].value    = varidx;
                expr->ec[expr_idx].fipslot  = pslot;

                if (expr_idx >= expr->allocated - 1)
                {
                    resize_expression_list (__FILE__, __LINE__, expr);
                }

                expr_idx++;
                expr->ec[expr_idx].obr      = 0;
                expr->ec[expr_idx].cbr      = 0;
                expr->ec[expr_idx].op       = 0;
            }
        }
        else if (type == KEYWORD_IS_OPERATOR)
        {
            // printf ("op(%s) ", kw);
            // printf ("expr_idx=%d last_keyword_was_operator=%d kw='%s'\n", expr_idx, last_keyword_was_operator, kw);

            if (! ustrcmp (kw, "-") && (last_keyword_was_operator == -1 || last_keyword_was_operator == 1))
            {
                // printf ("%d: expr_idx=%d last_keyword_was_operator=%d kw='%s'\n", line, expr_idx, last_keyword_was_operator, kw);

                if (negate_operand)
                {
                    fprintf (stderr, "error line %d: double negation not allowed (%d).\n", line, __LINE__);
                    rtc = EXPRESSION_ERROR;
                    break;
                }

                negate_operand = 1;
            }
            else if (! ustrcmp (kw, "~") && (last_keyword_was_operator == -1 || last_keyword_was_operator == 1))
            {
                // printf ("%d: expr_idx=%d last_keyword_was_operator=%d kw='%s'\n", line, expr_idx, last_keyword_was_operator, kw);

                if (invert_operand)
                {
                    fprintf (stderr, "error line %d: double inversion not allowed (%d).\n", line, __LINE__);
                    rtc = EXPRESSION_ERROR;
                    break;
                }

                invert_operand = 1;
            }
            else if (last_keyword_was_operator == 0)
            {
                last_keyword_was_operator = 1;
                expr->ec[expr_idx - 1].op = *kw;
            }
            else
            {
                fprintf (stderr, "error line %d: '%s': syntax error (%d).\n", line, kw, __LINE__);
                rtc = EXPRESSION_ERROR;
                break;
            }
        }
        else if (type == KEYWORD_IS_OPEN_BRACKET)
        {
            open_brackets++;
            last_keyword_was_operator = -1;                                             // needed for kw = "-", see 18 lines above
            expr->ec[expr_idx].obr++;
        }
        else if (type == KEYWORD_IS_CLOSE_BRACKET)
        {
            if (last_keyword_was_operator == 1)
            {
                fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                rtc = EXPRESSION_ERROR;
                break;
            }

            if (open_brackets)
            {
                open_brackets--;

                if (expr_idx > 0)
                {
                    expr->ec[expr_idx - 1].cbr++;
                }
                else
                {
                    fprintf (stderr, "error line %d: '%s': syntax error (%d).\n", line, kw, __LINE__);
                    rtc = EXPRESSION_ERROR;
                    break;
                }
            }
            else
            {
                fprintf (stderr, "error line %d: too many closing brackets.\n", line);
                rtc = EXPRESSION_ERROR;
                break;
            }
        }
        else if (type == KEYWORD_IS_EQUAL)
        {
            if (flag == WAITING_FOR_COMPARE_OPERATOR_FLAG)
            {
                rtc = EQUAL_COMPARE_OPERATOR;
            }
            else
            {
                fprintf (stderr, "error line %d: '=' unexpected.\n", line);
                rtc = EXPRESSION_ERROR;
            }
            break;
        }
        else if (type == KEYWORD_IS_NOT_EQUAL)
        {
            if (flag == WAITING_FOR_COMPARE_OPERATOR_FLAG)
            {
                rtc = NOT_EQUAL_COMPARE_OPERATOR;
            }
            else
            {
                fprintf (stderr, "error line %d: '!=' unexpected.\n", line);
                rtc = EXPRESSION_ERROR;
            }
            break;
        }
        else if (type == KEYWORD_IS_LESS)
        {
            if (flag == WAITING_FOR_COMPARE_OPERATOR_FLAG)
            {
                rtc = LESS_COMPARE_OPERATOR;
            }
            else
            {
                fprintf (stderr, "error line %d: '<' unexpected.\n", line);
                rtc = EXPRESSION_ERROR;
            }
            break;
        }
        else if (type == KEYWORD_IS_LESS_EQUAL)
        {
            if (flag == WAITING_FOR_COMPARE_OPERATOR_FLAG)
            {
                rtc = LESS_EQUAL_COMPARE_OPERATOR;
            }
            else
            {
                fprintf (stderr, "error line %d: '<=' unexpected.\n", line);
                rtc = EXPRESSION_ERROR;
            }
            break;
        }
        else if (type == KEYWORD_IS_GREATER)
        {
            if (flag == WAITING_FOR_COMPARE_OPERATOR_FLAG)
            {
                rtc = GREATER_COMPARE_OPERATOR;
            }
            else
            {
                fprintf (stderr, "error line %d: '>' unexpected.\n", line);
                rtc = EXPRESSION_ERROR;
            }
            break;
        }
        else if (type == KEYWORD_IS_GREATER_EQUAL)
        {
            if (flag == WAITING_FOR_COMPARE_OPERATOR_FLAG)
            {
                rtc = GREATER_EQUAL_COMPARE_OPERATOR;
            }
            else
            {
                fprintf (stderr, "error line %d: '>=' unexpected.\n", line);
                rtc = EXPRESSION_ERROR;
            }
            break;
        }
        else if (type == KEYWORD_IS_ARGUMENT_SEPARATOR)
        {
            last_keyword_was_operator = 0;
            // printf ("%s ", kw);
        }

        p = nextp;
    }

    if (rtc != EXPRESSION_ERROR)
    {
        if (open_brackets)
        {
            fprintf (stderr, "error line %d: too many open brackets.\n", line);
            rtc = EXPRESSION_ERROR;
        }
        else if (last_keyword_was_operator == 1)
        {
            fprintf (stderr, "error line %d: snytax error.\n", line);
            rtc = EXPRESSION_ERROR;
        }

        if (restp)
        {
            *restp = nextp;
        }
    }

    if (negate_operand)
    {
        fprintf (stderr, "error line %d: '-' unexpected.\n", line);
        rtc = EXPRESSION_ERROR;
    }

    if (invert_operand)
    {
        fprintf (stderr, "error line %d: '~' unexpected.\n", line);
        rtc = EXPRESSION_ERROR;
    }

    if (flag == WAITING_FOR_TO_OPERATOR_FLAG && ! to_keyword_found)
    {
        fprintf (stderr, "error line %d: missing keyword 'to'.\n", line);
        rtc = EXPRESSION_ERROR;
    }
    else if (flag == WAITING_FOR_STEP_OPERATOR_FLAG && step_keyword_found)
    {
        if (! *nextp)
        {
            fprintf (stderr, "error line %d: missing step value.\n", line);
            rtc = EXPRESSION_ERROR;
        }
    }

    return rtc;
}


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * push statement on stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
push_statement (STATEMENT_STACK * stackp)
{
    int rtc;

    if (statement_stack_depth < STATEMENT_STACK_DEPTH)
    {
        statement_stack[statement_stack_depth].type = stackp->type;
        statement_stack[statement_stack_depth].idx  = stackp->idx;
        statement_stack_depth++;
        rtc = OK;
    }
    else
    {
        rtc = ERR;
    }
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * pop statement from stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
pop_statement (STATEMENT_STACK * stackp)
{
    int rtc;

    if (statement_stack_depth > 0)
    {
        statement_stack_depth--;
        stackp->type    = statement_stack[statement_stack_depth].type;
        stackp->idx     = statement_stack[statement_stack_depth].idx;
        rtc = OK;
    }
    else
    {
        rtc = ERR;
    }
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * peek statement on stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
peek_statement (STATEMENT_STACK * stackp, int offset)
{
    int rtc;

    if (statement_stack_depth >= offset)
    {
        stackp->type    = statement_stack[statement_stack_depth - offset].type;
        stackp->idx     = statement_stack[statement_stack_depth - offset].idx;
        rtc = OK;
    }
    else
    {
        rtc = ERR;
    }
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * poke statement on stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
poke_statement (STATEMENT_STACK * stackp, int offset)
{
    int rtc;

    if (statement_stack_depth >= offset)
    {
        statement_stack[statement_stack_depth - offset].type    = stackp->type;
        statement_stack[statement_stack_depth - offset].idx     = stackp->idx;
        rtc = OK;
    }
    else
    {
        rtc = ERR;
    }
    return rtc;
}

#define BREAK_STACK_DEPTH           16

typedef struct
{
    int                             idx;
    int                             stack_idx;
} BREAK_STACK;

static BREAK_STACK                  break_stack[BREAK_STACK_DEPTH];
static int                          break_stack_depth = 0;

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * push break statement on break-stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
push_break (BREAK_STACK * stackp)
{
    int rtc;

    if (break_stack_depth < BREAK_STACK_DEPTH)
    {
        break_stack[break_stack_depth].idx          = stackp->idx;
        break_stack[break_stack_depth].stack_idx    = stackp->stack_idx;
        break_stack_depth++;
        rtc = OK;
    }
    else
    {
        rtc = ERR;
    }
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * pop break statement from break-stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
pop_break (BREAK_STACK * stackp)
{
    int rtc;

    if (break_stack_depth > 0)
    {
        break_stack_depth--;
        stackp->idx         = break_stack[break_stack_depth].idx;
        stackp->stack_idx   = break_stack[break_stack_depth].stack_idx;
        rtc = OK;
    }
    else
    {
        rtc = ERR;
    }
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * peek break statement on break-stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
peek_break (BREAK_STACK * stackp, int offset)
{
    int rtc;

    if (break_stack_depth >= offset)
    {
        stackp->idx         = break_stack[break_stack_depth - offset].idx;
        stackp->stack_idx   = break_stack[break_stack_depth - offset].stack_idx;
        rtc = OK;
    }
    else
    {
        rtc = ERR;
    }
    return rtc;
}

#define CONTINUE_STACK_DEPTH        16

typedef struct
{
    int                             idx;
    int                             stack_idx;
} CONTINUE_STACK;

static CONTINUE_STACK               continue_stack[CONTINUE_STACK_DEPTH];
static int                          continue_stack_depth = 0;

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * push continue statement on continue-stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
push_continue (CONTINUE_STACK * stackp)
{
    int rtc;

    if (continue_stack_depth < CONTINUE_STACK_DEPTH)
    {
        continue_stack[continue_stack_depth].idx        = stackp->idx;
        continue_stack[continue_stack_depth].stack_idx  = stackp->stack_idx;
        continue_stack_depth++;
        rtc = OK;
    }
    else
    {
        rtc = ERR;
    }
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * pop continue statement from continue-stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
pop_continue (CONTINUE_STACK * stackp)
{
    int rtc;

    if (continue_stack_depth > 0)
    {
        continue_stack_depth--;
        stackp->idx         = continue_stack[continue_stack_depth].idx;
        stackp->stack_idx   = continue_stack[continue_stack_depth].stack_idx;
        rtc = OK;
    }
    else
    {
        rtc = ERR;
    }
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * peek continue statement on continue-stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
peek_continue (CONTINUE_STACK * stackp, int offset)
{
    int rtc;

    if (continue_stack_depth >= offset)
    {
        stackp->idx         = continue_stack[continue_stack_depth - offset].idx;
        stackp->stack_idx   = continue_stack[continue_stack_depth - offset].stack_idx;
        rtc = OK;
    }
    else
    {
        rtc = ERR;
    }
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check all local variables, give a warning if not used
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
check_local_variables (int fidx)
{
    int         idx;
    FUNCTION *  fip;

    fip = functions + fidx;

    for (idx = 0; idx < fip->local_int_variables_used; idx++)
    {
        if (fip->local_int_variables[idx].used_cnt == 0)
        {
            if (fip->local_int_variables[idx].set_cnt > 0)
            {
                fprintf (stderr, "warning line %d: local int variable '%s' set but not used.\n",
                         fip->local_int_variables[idx].line, fip->local_int_variables[idx].name);
            }
            else
            {
                fprintf (stderr, "warning line %d: local int variable '%s' not used.\n",
                        fip->local_int_variables[idx].line, fip->local_int_variables[idx].name);
            }
        }
    }

    for (idx = 0; idx < fip->local_int_array_variables_used; idx++)
    {
        if (fip->local_int_array_variables[idx].used_cnt == 0)
        {
            if (fip->local_int_array_variables[idx].set_cnt > 0)
            {
                fprintf (stderr, "warning line %d: local int array variable '%s' set but not used.\n",
                         fip->local_int_array_variables[idx].line, fip->local_int_array_variables[idx].name);
            }
            else
            {
                fprintf (stderr, "warning line %d: local int array variable '%s' not used.\n",
                        fip->local_int_array_variables[idx].line, fip->local_int_array_variables[idx].name);
            }
        }
    }

    for (idx = 0; idx < fip->local_byte_variables_used; idx++)
    {
        if (fip->local_byte_variables[idx].used_cnt == 0)
        {
            if (fip->local_byte_variables[idx].set_cnt > 0)
            {
                fprintf (stderr, "warning line %d: local byte variable '%s' set but not used.\n",
                         fip->local_byte_variables[idx].line, fip->local_byte_variables[idx].name);
            }
            else
            {
                fprintf (stderr, "warning line %d: local byte variable '%s' not used.\n",
                        fip->local_byte_variables[idx].line, fip->local_byte_variables[idx].name);
            }
        }
    }

    for (idx = 0; idx < fip->local_byte_array_variables_used; idx++)
    {
        if (fip->local_byte_array_variables[idx].used_cnt == 0)
        {
            if (fip->local_byte_array_variables[idx].set_cnt > 0)
            {
                fprintf (stderr, "warning line %d: local byte array variable '%s' set but not used.\n",
                         fip->local_byte_array_variables[idx].line, fip->local_byte_array_variables[idx].name);
            }
            else
            {
                fprintf (stderr, "warning line %d: local byte array variable '%s' not used.\n",
                        fip->local_byte_array_variables[idx].line, fip->local_byte_array_variables[idx].name);
            }
        }
    }

    for (idx = 0; idx < fip->local_string_variables_used; idx++)
    {
        if (fip->local_string_variables[idx].used_cnt == 0)
        {
            if (fip->local_string_variables[idx].set_cnt > 0)
            {
                fprintf (stderr, "warning line %d: local string variable '%s' set but not used.\n",
                         fip->local_string_variables[idx].line, fip->local_string_variables[idx].name);
            }
            else
            {
                fprintf (stderr, "warning line %d: local string variable '%s' not used.\n",
                         fip->local_string_variables[idx].line, fip->local_string_variables[idx].name);
            }
        }
    }

    for (idx = 0; idx < fip->local_string_array_variables_used; idx++)
    {
        if (fip->local_string_array_variables[idx].used_cnt == 0)
        {
            if (fip->local_string_array_variables[idx].set_cnt > 0)
            {
                fprintf (stderr, "warning line %d: local string array variable '%s' set but not used.\n",
                         fip->local_string_array_variables[idx].line, fip->local_string_array_variables[idx].name);
            }
            else
            {
                fprintf (stderr, "warning line %d: local string array variable '%s' not used.\n",
                         fip->local_string_array_variables[idx].line, fip->local_string_array_variables[idx].name);
            }
        }
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check all undefined functions, give an error if definition cannot be found
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
check_undefined_functions (void)
{
    int                     idx;
    int                     pidx;
    int                     i;
    int                     func_idx;
    int                     new_func_idx;
    int                     rtc = OK;

    for (i = 0; i < postfix_slots_used; i++)
    {
        POSTFIX_ELEMENT * p = postfix_slots[i];

        pidx = 0;

        while (p[pidx].type != END)
        {
            if (p[pidx].type == OPERAND_UNDEFINED_FUNCTION)
            {
                FIP_RUN * fip = fip_run_slots[p[pidx].value];
                func_idx = fip->func_idx;

                if (func_idx >= undefined_functions_used)
                {
                    fprintf (stderr, "internal error in check_undefined_functions(), line %d: pidx=%d, value=%d func_idx=%d\n", __LINE__,
                            pidx, p[pidx].value, fip->func_idx);
                }

                new_func_idx = find_undefined_function (undefined_functions[func_idx].name);

                if (new_func_idx >= 0)
                {
                    undefined_functions[func_idx].used_cnt++;
                    functions[new_func_idx].used_cnt++;

                    if (functions[new_func_idx].return_type == FUNCTION_TYPE_VOID)
                    {
                        if (undefined_functions[func_idx].needs_return_value)
                        {
                            fprintf (stderr, "error line %d: function '%s' defined in line %d returns void.\n", undefined_functions[func_idx].line,
                                     undefined_functions[func_idx].name, functions[new_func_idx].line);
                            rtc = -1;
                        }
                    }

                    if (functions[new_func_idx].argc != undefined_functions[func_idx].argc)
                    {
                        fprintf (stderr, "error line %d: number of arguments wrong for call of function '%s' defined in line %d: got %d, expected %d.\n",
                                 undefined_functions[func_idx].line, functions[new_func_idx].name, functions[new_func_idx].line,
                                 undefined_functions[func_idx].argc, functions[new_func_idx].argc);
                        rtc = ERR;
                        break;
                    }

                    fip->func_idx = new_func_idx;
                    p[pidx].type = OPERAND_EXTERN_FUNCTION;
                }
            }

            pidx++;
        }
    }

    for (idx = 0; idx < undefined_functions_used; idx++)
    {
        if (undefined_functions[idx].used_cnt == 0)
        {
            fprintf (stderr, "error line %d: function '%s' undefined\n", undefined_functions[idx].line, undefined_functions[idx].name);
            rtc = ERR;
        }
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check all functions, give a warning if function is not used
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
check_functions (void)
{
    unsigned char   varname[2 * MAX_VARIABLE_NAME_LEN + 1];
    int             cnt;
    int             idx;
    int             vidx;

    for (idx = 0; idx < functions_used; idx++)
    {
        if (functions[idx].used_cnt == 0)
        {
            cnt = 0;

            sprintf ((char *) varname, "function.%s", functions[idx].name);
            vidx = find_global_int_variable (varname);

            if (vidx >= 0)
            {
                cnt = global_int_variables[vidx].used_cnt;
            }

            if (cnt == 0)
            {
                fprintf (stderr, "warning line %d: function '%s' defined in line %d not used.\n", functions[idx].line, functions[idx].name, functions[idx].line);
            }
        }
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check global variables, give a warning if variable is not used
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
check_global_variables (void)
{
    int         idx;

    for (idx = 0; idx < global_int_variables_used; idx++)
    {
        if (global_int_variables[idx].used_cnt == 0)
        {
            unsigned char * p = ustrchr (global_int_variables[idx].name, '.');

            if (p)
            {
                *p = '\0';
                if (ustrcmp (global_int_variables[idx].name, "function"))
                {
                    if (global_int_variables[idx].set_cnt > 0)
                    {
                        fprintf (stderr, "warning line %d: static int variable '%s' set but not used.\n", global_int_variables[idx].line, p + 1);
                    }
                    else
                    {
                        fprintf (stderr, "warning line %d: static int variable '%s' not used.\n", global_int_variables[idx].line, p + 1);
                    }
                }
                *p = '.';
            }
            else
            {
                if (global_int_variables[idx].set_cnt > 0)
                {
                    fprintf (stderr, "warning line %d: global int variable '%s' set but not used.\n",
                             global_int_variables[idx].line, global_int_variables[idx].name);
                }
                else
                {
                    fprintf (stderr, "warning line %d: global int variable '%s' not used.\n",
                             global_int_variables[idx].line, global_int_variables[idx].name);
                }
            }
        }
    }

    for (idx = 0; idx < global_int_array_variables_used; idx++)
    {
        if (global_int_array_variables[idx].used_cnt == 0)
        {
            unsigned char * p = ustrchr (global_int_array_variables[idx].name, '.');

            if (p)
            {
                *p = '\0';
                if (ustrcmp (global_int_array_variables[idx].name, "function"))
                {
                    fprintf (stderr, "warning line %d: static int array variable '%s' not used.\n", global_int_array_variables[idx].line, p + 1);
                }
                *p = '.';
            }
            else
            {
                if (global_int_array_variables[idx].set_cnt > 0)
                {
                    fprintf (stderr, "warning line %d: global int array variable '%s' set but not used.\n",
                             global_int_array_variables[idx].line, global_int_array_variables[idx].name);
                }
                else
                {
                    fprintf (stderr, "warning line %d: global int array variable '%s' not used.\n",
                             global_int_array_variables[idx].line, global_int_array_variables[idx].name);
                }
            }
        }
    }

    for (idx = 0; idx < global_byte_variables_used; idx++)
    {
        if (global_byte_variables[idx].used_cnt == 0)
        {
            unsigned char * p = ustrchr (global_byte_variables[idx].name, '.');

            if (p)
            {
                *p = '\0';
                if (ustrcmp (global_byte_variables[idx].name, "function"))
                {
                    fprintf (stderr, "warning line %d: static byte variable '%s' not used.\n", global_byte_variables[idx].line, p + 1);
                }
                *p = '.';
            }
            else
            {
                if (global_byte_variables[idx].set_cnt > 0)
                {
                    fprintf (stderr, "warning line %d: global byte variable '%s' set but not used.\n",
                             global_byte_variables[idx].line, global_byte_variables[idx].name);
                }
                else
                {
                    fprintf (stderr, "warning line %d: global byte variable '%s' not used.\n",
                             global_byte_variables[idx].line, global_byte_variables[idx].name);
                }
            }
        }
    }

    for (idx = 0; idx < global_byte_array_variables_used; idx++)
    {
        if (global_byte_array_variables[idx].used_cnt == 0)
        {
            unsigned char * p = ustrchr (global_byte_array_variables[idx].name, '.');

            if (p)
            {
                *p = '\0';
                if (ustrcmp (global_byte_array_variables[idx].name, "function"))
                {
                    fprintf (stderr, "warning line %d: static byte array variable '%s' not used.\n", global_byte_array_variables[idx].line, p + 1);
                }
                *p = '.';
            }
            else
            {
                if (global_byte_array_variables[idx].set_cnt > 0)
                {
                    fprintf (stderr, "warning line %d: global byte array variable '%s' set but not used.\n",
                             global_byte_array_variables[idx].line, global_byte_array_variables[idx].name);
                }
                else
                {
                    fprintf (stderr, "warning line %d: global byte array variable '%s' not used.\n",
                             global_byte_array_variables[idx].line, global_byte_array_variables[idx].name);
                }
            }
        }
    }

    for (idx = 0; idx < global_string_variables_used; idx++)
    {
        if (global_string_variables[idx].used_cnt == 0)
        {
            unsigned char * p = ustrchr (global_string_variables[idx].name, '.');

            if (p)
            {
                if (global_string_variables[idx].set_cnt > 0)
                {
                    fprintf (stderr, "warning line %d: static string variable '%s' set but not used.\n",
                             global_string_variables[idx].line, p + 1);
                }
                else
                {
                    fprintf (stderr, "warning line %d: static string variable '%s' not used.\n",
                             global_string_variables[idx].line, p + 1);
                }
            }
            else
            {
                if (global_string_variables[idx].set_cnt > 0)
                {
                    fprintf (stderr, "warning line %d: global string variable '%s' set not used.\n",
                             global_string_variables[idx].line, global_string_variables[idx].name);
                }
                else
                {
                    fprintf (stderr, "warning line %d: global string variable '%s' not used.\n",
                             global_string_variables[idx].line, global_string_variables[idx].name);
                }
            }
        }
    }

    for (idx = 0; idx < global_string_array_variables_used; idx++)
    {
        if (global_string_array_variables[idx].used_cnt == 0)
        {
            unsigned char * p = ustrchr (global_string_array_variables[idx].name, '.');

            if (p)
            {
                if (global_string_array_variables[idx].set_cnt > 0)
                {
                    fprintf (stderr, "warning line %d: static string array variable '%s' set but not used.\n",
                             global_string_array_variables[idx].line, p + 1);
                }
                else
                {
                    fprintf (stderr, "warning line %d: static string array variable '%s' not used.\n",
                             global_string_array_variables[idx].line, p + 1);
                }
            }
            else
            {
                if (global_string_array_variables[idx].set_cnt > 0)
                {
                    fprintf (stderr, "warning line %d: global string array variable '%s' set not used.\n",
                             global_string_array_variables[idx].line, global_string_array_variables[idx].name);
                }
                else
                {
                    fprintf (stderr, "warning line %d: global string array variable '%s' not used.\n",
                             global_string_array_variables[idx].line, global_string_array_variables[idx].name);
                }
            }
        }
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check const variables, give a warning if variable is not used
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
check_const_variables (void)
{
    int         idx;

    for (idx = 0; idx < const_int_variables_used; idx++)
    {
        if (const_int_variables[idx].used_cnt == 0)
        {
            unsigned char * p = ustrchr (const_int_variables[idx].name, '.');

            if (p)
            {
                *p = '\0';
                if (ustrcmp (const_int_variables[idx].name, "function"))
                {
                    fprintf (stderr, "warning line %d: const int variable '%s' not used.\n", const_int_variables[idx].line, p + 1);
                }
                *p = '.';
            }
            else
            {
                if (const_int_variables[idx].set_cnt > 0)
                {
                    fprintf (stderr, "warning line %d: const int variable '%s' set but not used.\n", const_int_variables[idx].line, const_int_variables[idx].name);
                }
                else
                {
                    fprintf (stderr, "warning line %d: const int variable '%s' not used.\n", const_int_variables[idx].line, const_int_variables[idx].name);
                }
            }
        }
    }

    for (idx = 0; idx < const_string_variables_used; idx++)
    {
        if (const_string_variables[idx].used_cnt == 0)
        {
            unsigned char * p = ustrchr (const_string_variables[idx].name, '.');

            if (p)
            {
                if (const_string_variables[idx].set_cnt > 0)
                {
                    fprintf (stderr, "warning line %d: static string variable '%s' set but not used.\n",
                             const_string_variables[idx].line, p + 1);
                }
                else
                {
                    fprintf (stderr, "warning line %d: static string variable '%s' not used.\n",
                             const_string_variables[idx].line, p + 1);
                }
            }
            else
            {
                if (const_string_variables[idx].set_cnt > 0)
                {
                    fprintf (stderr, "warning line %d: const string variable '%s' set but not used.\n",
                             const_string_variables[idx].line, const_string_variables[idx].name);
                }
                else
                {
                    fprintf (stderr, "warning line %d: const string variable '%s' not used.\n",
                             const_string_variables[idx].line, const_string_variables[idx].name);
                }
            }
        }
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check_initializer - check initializer
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
check_initializer (int line, unsigned char * s, unsigned char ** nextp,
                   int local_int_idx, int local_byte_idx, int local_str_idx,
                   int global_int_idx, int global_byte_idx, int global_str_idx,
                   int const_int_idx, int const_str_idx)
{
    unsigned char           kw[MAX_VARIABLE_NAME_LEN];
    int                     check;

    if ((check = check_keyword (kw, line, s, nextp, FALSE)) != KEYWORD_IS_EMPTY)
    {
        if (! ustrcmp (kw, "="))
        {
            s = *nextp;

            check = check_keyword (kw, line, s, nextp, TRUE);

            if (check == KEYWORD_IS_INT)
            {
                if (local_int_idx >= 0)
                {
                    EXPRESSION_CONTENT      expr;
                    POSTFIX_ELEMENT         postfix[MAX_POSTFIX_DEPTH];
                    int                     current_postfix_slot;

                    statementp[statements_used].line    = line;
                    statementp[statements_used].type    = STATEMENT_TYPE_INTERN_FUNCTION;
                    statementp[statements_used].next    = statements_used + 1;

                    statementp[statements_used].st.st_intern_function.assignment_variable_idx   = local_int_idx;
                    statementp[statements_used].st.st_intern_function.assignment_variable_type  = VARIABLE_TYPE_LOCAL_INT;

                    expr.type       = EXPRESSION_CONTENT_TYPE_INT_CONSTANT;
                    expr.obr        = 0;
                    expr.value      = uatoi (kw);
                    expr.cbr        = 0;
                    expr.op         = 0;                                                        // 0 terminates array of expressions!
                    expr.fipslot    = -1;

                    infix2postfix (postfix, &expr);
                    current_postfix_slot = new_postfix_slot (postfix);

                    if (current_postfix_slot < 0)
                    {
                        fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                        return -1;
                    }

                    statementp[statements_used].st.st_intern_function.postfix_slot = current_postfix_slot;

                    statements_used++;
                }
                else if (global_int_idx >= 0)
                {
                    global_int_variables[global_int_idx].v.int_value = uatoi (kw);
                }
                else if (const_int_idx >= 0)
                {
                    const_int_variables[const_int_idx].v.int_value = uatoi (kw);
                }
                else if (local_byte_idx >= 0)
                {
                    EXPRESSION_CONTENT      expr;
                    POSTFIX_ELEMENT         postfix[MAX_POSTFIX_DEPTH];
                    int                     current_postfix_slot;

                    statementp[statements_used].line    = line;
                    statementp[statements_used].type    = STATEMENT_TYPE_INTERN_FUNCTION;
                    statementp[statements_used].next    = statements_used + 1;

                    statementp[statements_used].st.st_intern_function.assignment_variable_idx   = local_byte_idx;
                    statementp[statements_used].st.st_intern_function.assignment_variable_type  = VARIABLE_TYPE_LOCAL_BYTE;

                    expr.type       = EXPRESSION_CONTENT_TYPE_INT_CONSTANT;
                    expr.obr        = 0;
                    expr.value      = uatoi (kw);
                    expr.cbr        = 0;
                    expr.op         = 0;                                                        // 0 terminates array of expressions!
                    expr.fipslot    = -1;

                    infix2postfix (postfix, &expr);
                    current_postfix_slot = new_postfix_slot (postfix);

                    if (current_postfix_slot < 0)
                    {
                        fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                        return -1;
                    }

                    statementp[statements_used].st.st_intern_function.postfix_slot = current_postfix_slot;

                    statements_used++;
                }
                else if (global_byte_idx >= 0)
                {
                    global_byte_variables[global_byte_idx].v.byte_value = uatoi (kw);
                }
                else
                {
                    fprintf (stderr, "error line %d: wrong initializer '%s' (%d).\n", line, kw, __LINE__);
                    return -1;
                }
            }
            else if (check == KEYWORD_IS_STRING)
            {
                if (local_str_idx >= 0)
                {
                    EXPRESSION_CONTENT      expr;
                    POSTFIX_ELEMENT         postfix[MAX_POSTFIX_DEPTH];
                    int                     current_postfix_slot;

                    statementp[statements_used].line    = line;
                    statementp[statements_used].type    = STATEMENT_TYPE_INTERN_FUNCTION;
                    statementp[statements_used].next    = statements_used + 1;

                    statementp[statements_used].st.st_intern_function.assignment_variable_idx   = local_str_idx;
                    statementp[statements_used].st.st_intern_function.assignment_variable_type  = VARIABLE_TYPE_LOCAL_STRING;

                    expr.type       = EXPRESSION_CONTENT_TYPE_STRING_CONSTANT;
                    expr.obr        = 0;
                    expr.value      = new_string_constant (kw);
                    expr.cbr        = 0;
                    expr.op         = 0;                                                        // 0 terminates array of expressions!
                    expr.fipslot    = -1;

                    infix2postfix (postfix, &expr);
                    current_postfix_slot = new_postfix_slot (postfix);

                    if (current_postfix_slot < 0)
                    {
                        fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                        return -1;
                    }

                    statementp[statements_used].st.st_intern_function.postfix_slot = current_postfix_slot;

                    statements_used++;
                }
                else if (global_str_idx >= 0)
                {
                    global_string_variables[global_str_idx].v.str_value = ustrdup (kw);
                }
                else if (const_str_idx >= 0)
                {
                    const_string_variables[const_str_idx].v.str_value = ustrdup (kw);
                }
                else
                {
                    fprintf (stderr, "error line %d: wrong initializer (%d).\n", line, __LINE__);
                    return -1;
                }
            }
            else if (check == KEYWORD_IS_EMPTY)
            {
                fprintf (stderr, "error line %d: missing initializer (%d).\n", line, __LINE__);
                return -1;
            }
            else
            {
                fprintf (stderr, "error line %d: unknown identifier '%s' as initializer (%d).\n", line, kw, __LINE__);
                return -1;
            }

            s = *nextp;
        }
        else
        {
            if (check != -1)
            {
                fprintf (stderr, "error line %d: syntax error - check = %d (%d).\n", line, check, __LINE__);
            }
            return -1;
        }
    }

    return 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * global_variable_exists - check if global variable exists
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
global_variable_exists (unsigned char * name)
{
    int idx;
    int line = 0;

    if ((idx = find_global_const_int_variable (name)) >= 0)
    {
        line = const_int_variables[idx].line;
    }
    else if ((idx = find_global_const_string_variable (name)) >= 0)
    {
        line = const_string_variables[idx].line;
    }
    else if ((idx = find_global_int_variable (name)) >= 0)
    {
        line = global_int_variables[idx].line;
    }
    else if ((idx = find_global_int_array_variable (name)) >= 0)
    {
        line = global_int_array_variables[idx].line;
    }
    else if ((idx = find_global_byte_variable (name)) >= 0)
    {
        line = global_byte_variables[idx].line;
    }
    else if ((idx = find_global_byte_array_variable (name)) >= 0)
    {
        line = global_byte_array_variables[idx].line;
    }
    else if ((idx = find_global_string_variable (name)) >= 0)
    {
        line = global_string_variables[idx].line;
    }
    else if ((idx = find_global_string_array_variable (name)) >= 0)
    {
        line = global_string_array_variables[idx].line;
    }
    return line;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * local_variable_exists - check if localvariable exists
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
local_variable_exists (FUNCTION * funcp, unsigned char * name)
{
    int idx;
    int line = 0;

    if ((idx = find_local_int_variable (funcp, name)) >= 0)
    {
        line = funcp->local_int_variables[idx].line;
    }
    else if ((idx = find_local_int_array_variable (funcp, name)) >= 0)
    {
        line = funcp->local_int_array_variables[idx].line;
    }
    else if ((idx = find_local_byte_variable (funcp, name)) >= 0)
    {
        line = funcp->local_byte_variables[idx].line;
    }
    else if ((idx = find_local_byte_array_variable (funcp, name)) >= 0)
    {
        line = funcp->local_byte_array_variables[idx].line;
    }
    else if ((idx = find_local_string_variable (funcp, name)) >= 0)
    {
        line = funcp->local_string_variables[idx].line;
    }
    else if ((idx = find_local_string_array_variable (funcp, name)) >= 0)
    {
        line = funcp->local_string_array_variables[idx].line;
    }
    else if ((idx = find_local_const_int_variable (funcp, name)) >= 0)
    {
        line = const_int_variables[idx].line;
    }
    else if ((idx = find_local_const_string_variable (funcp, name)) >= 0)
    {
        line = const_string_variables[idx].line;
    }
    else if ((idx = find_static_int_variable (funcp, name)) >= 0)
    {
        line = global_int_variables[idx].line;
    }
    else if ((idx = find_static_int_array_variable (funcp, name)) >= 0)
    {
        line = global_int_array_variables[idx].line;
    }
    else if ((idx = find_static_byte_variable (funcp, name)) >= 0)
    {
        line = global_byte_variables[idx].line;
    }
    else if ((idx = find_static_byte_array_variable (funcp, name)) >= 0)
    {
        line = global_byte_array_variables[idx].line;
    }
    else if ((idx = find_static_string_variable (funcp, name)) >= 0)
    {
        line = global_string_variables[idx].line;
    }
    else if ((idx = find_static_string_array_variable (funcp, name)) >= 0)
    {
        line = global_string_array_variables[idx].line;
    }

    return line;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * statement_calls_function - check if statement calls function
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
statement_calls_function (POSTFIX_ELEMENT * p)
{
    int idx = 0;

    while (p[idx].type != END)
    {
        if (p[idx].type == OPERAND_INTERN_FUNCTION || p[idx].type == OPERAND_EXTERN_FUNCTION || p[idx].type == OPERAND_UNDEFINED_FUNCTION)
        {
            return 1;
        }
        idx++;
    }
    return 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * statement_uses_variable - check if statement uses a variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
statement_uses_variable (int variable_idx, int variable_type, POSTFIX_ELEMENT * p)
{
    int idx = 0;
    int n   = 0;

    while (p[idx].type != END)
    {
        switch (variable_type)
        {
            case VARIABLE_TYPE_LOCAL_INT:
                if (p[idx].type == OPERAND_LOCAL_INT_VARIABLE && p[idx].value == variable_idx)
                {
                    n++;
                }
                break;
            case VARIABLE_TYPE_LOCAL_INT_ARRAY:
                if (p[idx].type == OPERAND_LOCAL_INT_ARRAY_VARIABLE && p[idx].value == variable_idx)
                {
                    n++;
                }
                break;
            case VARIABLE_TYPE_LOCAL_BYTE:
                if (p[idx].type == OPERAND_LOCAL_BYTE_VARIABLE && p[idx].value == variable_idx)
                {
                    n++;
                }
                break;
            case VARIABLE_TYPE_LOCAL_BYTE_ARRAY:
                if (p[idx].type == OPERAND_LOCAL_BYTE_ARRAY_VARIABLE && p[idx].value == variable_idx)
                {
                    n++;
                }
                break;
            case VARIABLE_TYPE_LOCAL_STRING:
                if (p[idx].type == OPERAND_LOCAL_STRING_VARIABLE && p[idx].value == variable_idx)
                {
                    n++;
                }
                break;
            case VARIABLE_TYPE_LOCAL_STRING_ARRAY:
                if (p[idx].type == OPERAND_LOCAL_STRING_ARRAY_VARIABLE && p[idx].value == variable_idx)
                {
                    n++;
                }
                break;
            case VARIABLE_TYPE_GLOBAL_INT:
                if (p[idx].type == OPERAND_GLOBAL_INT_VARIABLE && p[idx].value == variable_idx)
                {
                    n++;
                }
                break;
            case VARIABLE_TYPE_GLOBAL_INT_ARRAY:
                if (p[idx].type == OPERAND_GLOBAL_INT_ARRAY_VARIABLE && p[idx].value == variable_idx)
                {
                    n++;
                }
                break;
            case VARIABLE_TYPE_GLOBAL_BYTE:
                if (p[idx].type == OPERAND_GLOBAL_BYTE_VARIABLE && p[idx].value == variable_idx)
                {
                    n++;
                }
                break;
            case VARIABLE_TYPE_GLOBAL_BYTE_ARRAY:
                if (p[idx].type == OPERAND_GLOBAL_BYTE_ARRAY_VARIABLE && p[idx].value == variable_idx)
                {
                    n++;
                }
                break;
            case VARIABLE_TYPE_GLOBAL_STRING:
                if (p[idx].type == OPERAND_GLOBAL_STRING_VARIABLE && p[idx].value == variable_idx)
                {
                    n++;
                }
                break;
            case VARIABLE_TYPE_GLOBAL_STRING_ARRAY:
                if (p[idx].type == OPERAND_GLOBAL_STRING_ARRAY_VARIABLE && p[idx].value == variable_idx)
                {
                    n++;
                }
                break;
        }

        idx++;
    }
    return n;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * statement_is_increment_variable - check statement increments only a variable
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
statement_is_increment_variable (int assignment_variable_idx, int assignment_variable_type, POSTFIX_ELEMENT * postfixp)
{
    int rtc = 0;

    if (assignment_variable_idx >= 0)
    {
        if (assignment_variable_type == VARIABLE_TYPE_LOCAL_INT)
        {
            if (postfixp[0].type == OPERAND_LOCAL_INT_VARIABLE && postfixp[0].value == assignment_variable_idx &&                   // v = v + const
                postfixp[1].type == OPERAND_INT_CONSTANT &&
                postfixp[2].type == OPERATOR && postfixp[2].value == '+' &&
                postfixp[3].type == END)
            {
                rtc = postfixp[1].value;                                                                                            // const
            }
            else if (postfixp[0].type == OPERAND_LOCAL_INT_VARIABLE && postfixp[0].value == assignment_variable_idx &&              // v = v - const
                     postfixp[1].type == OPERAND_INT_CONSTANT &&
                     postfixp[2].type == OPERATOR && postfixp[2].value == '-' &&
                     postfixp[3].type == END)
            {
                rtc = -postfixp[1].value;                                                                                           // const
            }
            else if (postfixp[0].type == OPERAND_INT_CONSTANT &&                                                                    // v = const + v
                     postfixp[1].type == OPERAND_LOCAL_INT_VARIABLE && postfixp[1].value == assignment_variable_idx &&
                     postfixp[2].type == OPERATOR && postfixp[2].value == '+' &&
                     postfixp[3].type == END)
            {
                rtc = postfixp[0].value;                                                                                            // const
            }
        }
        else if (assignment_variable_type == VARIABLE_TYPE_GLOBAL_INT)
        {
            if (postfixp[0].type == OPERAND_GLOBAL_INT_VARIABLE && postfixp[0].value == assignment_variable_idx &&                  // v = v + const
                postfixp[1].type == OPERAND_INT_CONSTANT &&
                postfixp[2].type == OPERATOR && postfixp[2].value == '+' &&
                postfixp[3].type == END)
            {
                rtc = postfixp[1].value;                                                                                            // const
            }
            else if (postfixp[0].type == OPERAND_GLOBAL_INT_VARIABLE && postfixp[0].value == assignment_variable_idx &&             // v = v - const
                     postfixp[1].type == OPERAND_INT_CONSTANT &&
                     postfixp[2].type == OPERATOR && postfixp[2].value == '-' &&
                     postfixp[3].type == END)
            {
                rtc = -postfixp[1].value;                                                                                           // const
            }
            else if (postfixp[0].type == OPERAND_INT_CONSTANT &&                                                                    // v = const + v
                     postfixp[1].type == OPERAND_GLOBAL_INT_VARIABLE && postfixp[1].value == assignment_variable_idx &&
                     postfixp[2].type == OPERATOR && postfixp[2].value == '+' &&
                     postfixp[3].type == END)
            {
                rtc = postfixp[0].value;                                                                                            // const
            }
        }
        else if (assignment_variable_type == VARIABLE_TYPE_LOCAL_BYTE)
        {
            if (postfixp[0].type == OPERAND_LOCAL_BYTE_VARIABLE && postfixp[0].value == assignment_variable_idx &&                  // v = v + const
                postfixp[1].type == OPERAND_INT_CONSTANT &&
                postfixp[2].type == OPERATOR && postfixp[2].value == '+' &&
                postfixp[3].type == END)
            {
                rtc = postfixp[1].value;                                                                                            // const
            }
            else if (postfixp[0].type == OPERAND_LOCAL_BYTE_VARIABLE && postfixp[0].value == assignment_variable_idx &&             // v = v - const
                     postfixp[1].type == OPERAND_INT_CONSTANT &&
                     postfixp[2].type == OPERATOR && postfixp[2].value == '-' &&
                     postfixp[3].type == END)
            {
                rtc = -postfixp[1].value;                                                                                           // const
            }
            else if (postfixp[0].type == OPERAND_INT_CONSTANT &&                                                                    // v = const + v
                     postfixp[1].type == OPERAND_LOCAL_BYTE_VARIABLE && postfixp[1].value == assignment_variable_idx &&
                     postfixp[2].type == OPERATOR && postfixp[2].value == '+' &&
                     postfixp[3].type == END)
            {
                rtc = postfixp[0].value;                                                                                            // const
            }
        }
        else if (assignment_variable_type == VARIABLE_TYPE_GLOBAL_BYTE)
        {
            if (postfixp[0].type == OPERAND_GLOBAL_BYTE_VARIABLE && postfixp[0].value == assignment_variable_idx &&                 // v = v + const
                postfixp[1].type == OPERAND_INT_CONSTANT &&
                postfixp[2].type == OPERATOR && postfixp[2].value == '+' &&
                postfixp[3].type == END)
            {
                rtc = postfixp[1].value;                                                                                            // const
            }
            else if (postfixp[0].type == OPERAND_GLOBAL_BYTE_VARIABLE && postfixp[0].value == assignment_variable_idx &&            // v = v - const
                     postfixp[1].type == OPERAND_INT_CONSTANT &&
                     postfixp[2].type == OPERATOR && postfixp[2].value == '-' &&
                     postfixp[3].type == END)
            {
                rtc = -postfixp[1].value;                                                                                           // const
            }
            else if (postfixp[0].type == OPERAND_INT_CONSTANT &&                                                                    // v = const + v
                     postfixp[1].type == OPERAND_GLOBAL_BYTE_VARIABLE && postfixp[1].value == assignment_variable_idx &&
                     postfixp[2].type == OPERATOR && postfixp[2].value == '+' &&
                     postfixp[3].type == END)
            {
                rtc = postfixp[0].value;                                                                                            // const
            }
        }
    }
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * is_const_int_variable - check if variable is of type const
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
is_const_int_variable (unsigned char * kw, int * valp)
{
    int varidx;
    int rtc = 0;

    if (in_function && (varidx = find_local_const_int_variable (functions + current_function_idx, kw)) >= 0)
    {
        const_int_variables[varidx].used_cnt++;
        *valp = const_int_variables[varidx].v.int_value;
        rtc = 1;
    }
    else if ((varidx = find_global_const_int_variable (kw)) >= 0)
    {
        const_int_variables[varidx].used_cnt++;
        *valp = const_int_variables[varidx].v.int_value;
        rtc = 1;
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nicc - the compiler main loop
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nicc (const char * in, int verbose)
{
    EXPRESSION_LIST *       expr;
    POSTFIX_ELEMENT         postfix[MAX_POSTFIX_DEPTH];
    unsigned char           buf[BUFLEN];
    FILE *                  fp;
    unsigned char *         p;
    unsigned char *         pp;
    int                     t;
    int                     line = 0;
    int                     assignment_variable_idx;
    int                     assignment_variable_type;
    char                    assignment_variable[MAX_VARIABLE_NAME_LEN];
    unsigned char           kw[MAX_VARIABLE_NAME_LEN];
    int                     rtc = 0;

    fp = fopen (in, "r");

    if (fp)
    {
        expr = new_expression_list (__FILE__, __LINE__);

        statements_allocated = STATEMENT_ALLOC_GRANULARITY;
        statementp = alloc_calloc (__FILE__, __LINE__, statements_allocated, sizeof (STATEMENT));

        while (fgets ((char *) buf, BUFLEN, fp))
        {
            line++;

            if (statements_used >= statements_allocated)
            {
                if (statements_used > statements_allocated)
                {
                    fprintf (stderr, "internal error 1\n");
                    rtc = -1;
                    break;
                }

                statementp = alloc_realloc (__FILE__, __LINE__, statementp, (statements_allocated + STATEMENT_ALLOC_GRANULARITY) * sizeof (STATEMENT));
                memset (statementp + statements_allocated, 0, STATEMENT_ALLOC_GRANULARITY * sizeof (STATEMENT));
                statements_allocated += STATEMENT_ALLOC_GRANULARITY;
            }

            assignment_variable_idx = -1;

            if ((p = ustrchr (buf, '\r')) != NULL)
            {
                *p = '\0';
            }
            if ((p = ustrchr (buf, '\n')) != NULL)
            {
                *p = '\0';
            }

            p = buf;

            t = check_keyword (kw, line, p, &pp, FALSE);

            if (t == KEYWORD_IS_EMPTY)
            {
                continue;
            }

            if (t != KEYWORD_IS_IDENTIFIER)
            {
                fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                rtc = -1;
                break;
            }

            p = pp;

            if (! ustrcmp (kw, "function"))
            {
                unsigned char * restp;

                if (in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected. Please terminate previous function '%s' defined in line %d before.\n",
                            line, kw, functions[current_function_idx].name, functions[current_function_idx].line);
                    rtc = -1;
                    break;
                }

                in_function = 1;

                if (handle_expression (line, expr, p, IS_FUNCTION_DEFINITION_FLAG, &restp) == EXPRESSION_ERROR)
                {
                    rtc = -1;
                    break;
                }

                continue;                                               // if we call handle_expression(), we musst call continue here
            }
            else if (! ustrcmp (kw, "endfunction"))
            {
                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                // if function has empty body or previous statement is no return statement, we have to insert one or to give error
                if (statements_used == 0 ||
                    (statements_used > 0 &&
                    (statements_used == functions[current_function_idx].first_statement_idx || statementp[statements_used - 1].type != STATEMENT_TYPE_RETURN)))
                {
                    if (functions[current_function_idx].return_type == FUNCTION_TYPE_VOID)              // no return in void function is okay
                    {
                        statementp[statements_used].line    = line;
                        statementp[statements_used].type    = STATEMENT_TYPE_RETURN;
                        statementp[statements_used].next    = statements_used + 1;
                        statementp[statements_used].st.st_return.postfix_slot = -1;
                        statements_used++;
                    }
                    else
                    {
                        fprintf (stderr, "error line %d: missing return before 'endfunction'.\n", line);
                        rtc = -1;
                        break;
                    }
                }

                check_local_variables (current_function_idx);
                in_function = 0;
                p = pp;
            }
            else if (! ustrcmp (kw, "const"))
            {
                int     tmpline;
                int     const_int_idx = -1;
                int     const_str_idx = -1;

                p = pp;

                if (check_keyword (kw, line, p, &pp, FALSE) != KEYWORD_IS_IDENTIFIER)
                {
                    fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                    rtc = -1;
                    break;
                }

                p = pp;

                if (! ustrcmp (kw, "int"))  // const int
                {
                    if (check_keyword (kw, line, p, &pp, FALSE) != KEYWORD_IS_IDENTIFIER)
                    {
                        fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                        rtc = -1;
                        break;
                    }

                    p = pp;

                    if (in_function)
                    {
                        unsigned char varname[MAX_FUNCTION_NAME_LEN + MAX_VARIABLE_NAME_LEN + 2];

                        if ((tmpline = local_variable_exists (functions + current_function_idx, kw)) > 0)
                        {
                            fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                            rtc = -1;
                            break;
                        }

                        if ((tmpline = global_variable_exists (kw)) > 0)
                        {
                            fprintf (stderr, "warning line %d: variable '%s' shadows global variable '%s' defined in line %d.\n", line, kw, kw, tmpline);
                        }

                        sprintf ((char *) varname, "%s.%s", functions[current_function_idx].name, kw);

                        const_int_idx = new_const_int_variable (varname, line);
                    }
                    else // global const
                    {
                        if ((tmpline = global_variable_exists (kw)) > 0)
                        {
                            fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                            rtc = -1;
                            break;
                        }

                        const_int_idx = new_const_int_variable (kw, line);
                    }
                }
                else if (! ustrcmp (kw, "string"))  // const string
                {
                    if (check_keyword (kw, line, p, &pp, FALSE) != KEYWORD_IS_IDENTIFIER)
                    {
                        fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                        rtc = -1;
                        break;
                    }

                    if (in_function)
                    {
                        unsigned char varname[MAX_FUNCTION_NAME_LEN + MAX_VARIABLE_NAME_LEN + 2];

                        if ((tmpline = local_variable_exists (functions + current_function_idx, kw)) > 0)
                        {
                            fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                            rtc = -1;
                            break;
                        }

                        if ((tmpline = global_variable_exists (kw)) > 0)
                        {
                            fprintf (stderr, "warning line %d: variable '%s' shadows global variable '%s' defined in line %d.\n", line, kw, kw, tmpline);
                        }

                        sprintf ((char *) varname, "%s.%s", functions[current_function_idx].name, kw);
                        const_str_idx = new_const_string_variable (varname, line);
                    }
                    else // global const
                    {
                        if ((tmpline = global_variable_exists (kw)) > 0)
                        {
                            fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                            rtc = -1;
                            break;
                        }

                        const_str_idx = new_const_string_variable (kw, line);
                    }
                }
                else
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                p = pp;

                if (check_initializer (line, p, &pp, -1, -1, -1, -1, -1, -1, const_int_idx, const_str_idx) < 0)
                {
                    rtc = -1;
                    break;
                }

                p = pp;
            }
            else if (! ustrcmp (kw, "static"))
            {
                unsigned char   dim[MAX_VARIABLE_NAME_LEN];
                int             arraysize = 0;
                int             tmpline;
                int             global_int_idx = -1;
                int             global_byte_idx = -1;
                int             global_str_idx = -1;

                p = pp;

                if (check_keyword (kw, line, p, &pp, FALSE) != KEYWORD_IS_IDENTIFIER)
                {
                    fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                    rtc = -1;
                    break;
                }

                p = pp;

                if (! ustrcmp (kw, "int"))  // static int
                {
                    if (check_keyword (kw, line, p, &pp, FALSE) != KEYWORD_IS_IDENTIFIER)
                    {
                        fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                        rtc = -1;
                        break;
                    }

                    p = pp;

                    if (check_keyword (dim, line, p, &pp, FALSE) == KEYWORD_IS_OPEN_SQUARE_BRACKET)
                    {
                        int     kwtype;

                        p = pp;

                        kwtype = check_keyword (dim, line, p, &pp, FALSE);

                        if (kwtype == KEYWORD_IS_INT)
                        {
                            arraysize = uatoi (dim);
                        }
                        else if (! (kwtype == KEYWORD_IS_IDENTIFIER && is_const_int_variable (dim, &arraysize)))
                        {
                            fprintf (stderr, "error line %d: '%s': constant integer for arraysize of array expected.\n", line, dim);
                            rtc = -1;
                            break;
                        }

                        p = pp;

                        if (check_keyword (dim, line, p, &pp, FALSE) != KEYWORD_IS_CLOSE_SQUARE_BRACKET)
                        {
                            fprintf (stderr, "error line %d: '%s': constant integer for arraysize of array expected.\n", line, dim);
                            rtc = -1;
                            break;
                        }
                    }
                    else
                    {
                        pp = p;
                    }

                    if (in_function)
                    {
                        unsigned char varname[MAX_FUNCTION_NAME_LEN + MAX_VARIABLE_NAME_LEN + 2];

                        if ((tmpline = local_variable_exists (functions + current_function_idx, kw)) > 0)
                        {
                            fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                            rtc = -1;
                            break;
                        }

                        if ((tmpline = global_variable_exists (kw)) > 0)
                        {
                            fprintf (stderr, "warning line %d: variable '%s' shadows global variable '%s' defined in line %d.\n", line, kw, kw, tmpline);
                        }

                        sprintf ((char *) varname, "%s.%s", functions[current_function_idx].name, kw);

                        if (arraysize)
                        {
                            global_int_idx = new_global_int_array_variable (varname, arraysize, line);
                        }
                        else
                        {
                            global_int_idx = new_global_int_variable (varname, line);
                        }

                    }
                    else // global: ignore keyword 'static'
                    {
                        fprintf (stderr, "warning line %d: keyword 'static' takes no effect here.\n",  line);

                        if ((tmpline = global_variable_exists (kw)) > 0)
                        {
                            fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                            rtc = -1;
                            break;
                        }

                        if (arraysize)
                        {
                            global_int_idx = new_global_int_array_variable (kw, arraysize, line);
                        }
                        else
                        {
                            global_int_idx = new_global_int_variable (kw, line);
                        }
                    }
                }
                else if (! ustrcmp (kw, "byte"))  // static byte
                {
                    if (check_keyword (kw, line, p, &pp, FALSE) != KEYWORD_IS_IDENTIFIER)
                    {
                        fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                        rtc = -1;
                        break;
                    }

                    p = pp;

                    if (check_keyword (dim, line, p, &pp, FALSE) == KEYWORD_IS_OPEN_SQUARE_BRACKET)
                    {
                        int     kwtype;

                        p = pp;

                        kwtype = check_keyword (dim, line, p, &pp, FALSE);

                        if (kwtype == KEYWORD_IS_INT)
                        {
                            arraysize = uatoi (dim);
                        }
                        else if (! (kwtype == KEYWORD_IS_IDENTIFIER && is_const_int_variable (dim, &arraysize)))
                        {
                            fprintf (stderr, "error line %d: '%s': constant integer for arraysize of array expected.\n", line, dim);
                            rtc = -1;
                            break;
                        }

                        p = pp;

                        if (check_keyword (dim, line, p, &pp, FALSE) != KEYWORD_IS_CLOSE_SQUARE_BRACKET)
                        {
                            fprintf (stderr, "error line %d: '%s': constant integer for arraysize of array expected.\n", line, dim);
                            rtc = -1;
                            break;
                        }
                    }
                    else
                    {
                        pp = p;
                    }

                    if (in_function)
                    {
                        unsigned char varname[MAX_FUNCTION_NAME_LEN + MAX_VARIABLE_NAME_LEN + 2];

                        if ((tmpline = local_variable_exists (functions + current_function_idx, kw)) > 0)
                        {
                            fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                            rtc = -1;
                            break;
                        }

                        if ((tmpline = global_variable_exists (kw)) > 0)
                        {
                            fprintf (stderr, "warning line %d: variable '%s' shadows global variable '%s' defined in line %d.\n", line, kw, kw, tmpline);
                        }

                        sprintf ((char *) varname, "%s.%s", functions[current_function_idx].name, kw);

                        if (arraysize)
                        {
                            global_byte_idx = new_global_byte_array_variable (varname, arraysize, line);
                        }
                        else
                        {
                            global_byte_idx = new_global_byte_variable (varname, line);
                        }

                    }
                    else // global: ignore keyword 'static'
                    {
                        fprintf (stderr, "warning line %d: keyword 'static' takes no effect here.\n",  line);

                        if ((tmpline = global_variable_exists (kw)) > 0)
                        {
                            fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                            rtc = -1;
                            break;
                        }

                        if (arraysize)
                        {
                            global_byte_idx = new_global_byte_array_variable (kw, arraysize, line);
                        }
                        else
                        {
                            global_byte_idx = new_global_byte_variable (kw, line);
                        }
                    }
                }
                else if (! ustrcmp (kw, "string"))  // static string
                {
                    if (check_keyword (kw, line, p, &pp, FALSE) != KEYWORD_IS_IDENTIFIER)
                    {
                        fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                        rtc = -1;
                        break;
                    }

                    p = pp;

                    if (check_keyword (dim, line, p, &pp, FALSE) == KEYWORD_IS_OPEN_SQUARE_BRACKET)
                    {
                        int     kwtype;

                        p = pp;

                        kwtype = check_keyword (dim, line, p, &pp, FALSE);

                        if (kwtype == KEYWORD_IS_INT)
                        {
                            arraysize = uatoi (dim);
                        }
                        else if (! (kwtype == KEYWORD_IS_IDENTIFIER && is_const_int_variable (dim, &arraysize)))
                        {
                            fprintf (stderr, "error line %d: '%s': constant integer for arraysize of array expected.\n", line, dim);
                            rtc = -1;
                            break;
                        }

                        p = pp;

                        if (check_keyword (dim, line, p, &pp, FALSE) != KEYWORD_IS_CLOSE_SQUARE_BRACKET)
                        {
                            fprintf (stderr, "error line %d: '%s': constant integer for arraysize of array expected.\n", line, dim);
                            rtc = -1;
                            break;
                        }
                    }
                    else
                    {
                        pp = p;
                    }

                    if (in_function)
                    {
                        unsigned char varname[MAX_FUNCTION_NAME_LEN + MAX_VARIABLE_NAME_LEN + 2];

                        if ((tmpline = local_variable_exists (functions + current_function_idx, kw)) > 0)
                        {
                            fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                            rtc = -1;
                            break;
                        }

                        if ((tmpline = global_variable_exists (kw)) > 0)
                        {
                            fprintf (stderr, "warning line %d: variable '%s' shadows global variable '%s' defined in line %d.\n", line, kw, kw, tmpline);
                        }

                        sprintf ((char *) varname, "%s.%s", functions[current_function_idx].name, kw);

                        if (arraysize)
                        {
                            global_str_idx = new_global_string_array_variable (varname, arraysize, line);
                        }
                        else
                        {
                            global_str_idx = new_global_string_variable (varname, line);
                        }
                    }
                    else // global: ignore keyword 'static'
                    {
                        fprintf (stderr, "warning line %d: keyword 'static' takes no effect here.\n",  line);

                        if ((tmpline = global_variable_exists (kw)) > 0)
                        {
                            fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                            rtc = -1;
                            break;
                        }

                        if (arraysize)
                        {
                            global_str_idx = new_global_string_array_variable (kw, arraysize, line);
                        }
                        else
                        {
                            global_str_idx = new_global_string_variable (kw, line);
                        }
                    }
                }
                else
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                p = pp;

                if (arraysize == 0)
                {
                    if (check_initializer (line, p, &pp, -1, -1, -1, global_int_idx, global_byte_idx, global_str_idx, -1, -1) < 0)
                    {
                        rtc = -1;
                        break;
                    }
                    p = pp;
                }
            }
            else if (! ustrcmp (kw, "int"))
            {
                unsigned char   dim[MAX_VARIABLE_NAME_LEN];
                int             arraysize = 0;
                int             tmpline;

                if (check_keyword (kw, line, p, &pp, FALSE) != KEYWORD_IS_IDENTIFIER)
                {
                    fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                    rtc = -1;
                    break;
                }

                p = pp;

                if (check_keyword (dim, line, p, &pp, FALSE) == KEYWORD_IS_OPEN_SQUARE_BRACKET)
                {
                    int     kwtype;

                    p = pp;

                    kwtype = check_keyword (dim, line, p, &pp, FALSE);

                    if (kwtype == KEYWORD_IS_INT)
                    {
                        arraysize = uatoi (dim);
                    }
                    else if (! (kwtype == KEYWORD_IS_IDENTIFIER && is_const_int_variable (dim, &arraysize)))
                    {
                        fprintf (stderr, "error line %d: '%s': constant integer for arraysize of array expected.\n", line, dim);
                        rtc = -1;
                        break;
                    }

                    p = pp;

                    if (check_keyword (dim, line, p, &pp, FALSE) != KEYWORD_IS_CLOSE_SQUARE_BRACKET)
                    {
                        fprintf (stderr, "error line %d: '%s': constant integer for arraysize of array expected.\n", line, dim);
                        rtc = -1;
                        break;
                    }
                }
                else
                {
                    pp = p;
                }

                if (in_function)
                {
                    int             local_int_idx;

                    if ((tmpline = local_variable_exists (functions + current_function_idx, kw)) > 0)
                    {
                        fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                        rtc = -1;
                        break;
                    }

                    if ((tmpline = global_variable_exists (kw)) > 0)
                    {
                        fprintf (stderr, "warning line %d: variable '%s' shadows global variable '%s' defined in line %d.\n", line, kw, kw, tmpline);
                    }

                    if (arraysize)
                    {
                        local_int_idx = new_local_int_array_variable (functions + current_function_idx, kw, arraysize, line);
                    }
                    else
                    {
                        local_int_idx = new_local_int_variable (functions + current_function_idx, kw, line);
                    }

                    p = pp;

                    if (arraysize == 0)
                    {
                        if (check_initializer (line, p, &pp, local_int_idx, -1, -1, -1, -1, -1, -1, -1) < 0)
                        {
                            rtc = -1;
                            break;
                        }

                        p = pp;
                    }
                }
                else
                {
                    int     global_int_idx;

                    if ((tmpline = global_variable_exists (kw)) > 0)
                    {
                        fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                        rtc = -1;
                        break;
                    }

                    if (arraysize)
                    {
                        global_int_idx = new_global_int_array_variable (kw, arraysize, line);
                    }
                    else
                    {
                        global_int_idx = new_global_int_variable (kw, line);
                    }

                    p = pp;

                    if (arraysize == 0)
                    {
                        if (check_initializer (line, p, &pp, -1, -1, -1, global_int_idx, -1, -1, -1, -1) < 0)
                        {
                            rtc = -1;
                            break;
                        }
                    }
                }
                p = pp;
            }
            else if (! ustrcmp (kw, "byte"))
            {
                unsigned char   dim[MAX_VARIABLE_NAME_LEN];
                int             arraysize = 0;
                int             tmpline;

                if (check_keyword (kw, line, p, &pp, FALSE) != KEYWORD_IS_IDENTIFIER)
                {
                    fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                    rtc = -1;
                    break;
                }

                p = pp;

                if (check_keyword (dim, line, p, &pp, FALSE) == KEYWORD_IS_OPEN_SQUARE_BRACKET)
                {
                    int     kwtype;

                    p = pp;

                    kwtype = check_keyword (dim, line, p, &pp, FALSE);

                    if (kwtype == KEYWORD_IS_INT)
                    {
                        arraysize = uatoi (dim);
                    }
                    else if (! (kwtype == KEYWORD_IS_IDENTIFIER && is_const_int_variable (dim, &arraysize)))
                    {
                        fprintf (stderr, "error line %d: '%s': constant integer for arraysize of array expected.\n", line, dim);
                        rtc = -1;
                        break;
                    }

                    p = pp;

                    if (check_keyword (dim, line, p, &pp, FALSE) != KEYWORD_IS_CLOSE_SQUARE_BRACKET)
                    {
                        fprintf (stderr, "error line %d: '%s': constant integer for arraysize of array expected.\n", line, dim);
                        rtc = -1;
                        break;
                    }
                }
                else
                {
                    pp = p;
                }

                if (in_function)
                {
                    int             local_byte_idx;

                    if ((tmpline = local_variable_exists (functions + current_function_idx, kw)) > 0)
                    {
                        fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                        rtc = -1;
                        break;
                    }

                    if ((tmpline = global_variable_exists (kw)) > 0)
                    {
                        fprintf (stderr, "warning line %d: variable '%s' shadows global variable '%s' defined in line %d.\n", line, kw, kw, tmpline);
                    }

                    if (arraysize)
                    {
                        local_byte_idx = new_local_byte_array_variable (functions + current_function_idx, kw, arraysize, line);
                    }
                    else
                    {
                        local_byte_idx = new_local_byte_variable (functions + current_function_idx, kw, line);
                    }

                    p = pp;

                    if (arraysize == 0)
                    {
                        if (check_initializer (line, p, &pp, -1, local_byte_idx, -1, -1, -1, -1, -1, -1) < 0)
                        {
                            rtc = -1;
                            break;
                        }

                        p = pp;
                    }
                }
                else
                {
                    int     global_byte_idx;

                    if ((tmpline = global_variable_exists (kw)) > 0)
                    {
                        fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                        rtc = -1;
                        break;
                    }

                    if (arraysize)
                    {
                        global_byte_idx = new_global_byte_array_variable (kw, arraysize, line);
                    }
                    else
                    {
                        global_byte_idx = new_global_byte_variable (kw, line);
                    }

                    p = pp;

                    if (arraysize == 0)
                    {
                        if (check_initializer (line, p, &pp, -1, -1, -1, -1, global_byte_idx, -1, -1, -1) < 0)
                        {
                            rtc = -1;
                            break;
                        }
                    }
                }
                p = pp;
            }
            else if (! ustrcmp (kw, "string"))
            {
                unsigned char   dim[MAX_VARIABLE_NAME_LEN];
                int             arraysize = 0;
                int             tmpline;

                if (check_keyword (kw, line, p, &pp, FALSE) != KEYWORD_IS_IDENTIFIER)
                {
                    fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                    rtc = -1;
                    break;
                }

                p = pp;

                if (check_keyword (dim, line, p, &pp, FALSE) == KEYWORD_IS_OPEN_SQUARE_BRACKET)
                {
                    int     kwtype;

                    p = pp;

                    kwtype = check_keyword (dim, line, p, &pp, FALSE);

                    if (kwtype == KEYWORD_IS_INT)
                    {
                        arraysize = uatoi (dim);
                    }
                    else if (! (kwtype == KEYWORD_IS_IDENTIFIER && is_const_int_variable (dim, &arraysize)))
                    {
                        fprintf (stderr, "error line %d: '%s': constant integer for arraysize of array expected.\n", line, dim);
                        rtc = -1;
                        break;
                    }

                    p = pp;

                    if (check_keyword (dim, line, p, &pp, FALSE) != KEYWORD_IS_CLOSE_SQUARE_BRACKET)
                    {
                        fprintf (stderr, "error line %d: '%s': constant integer for arraysize of array expected.\n", line, dim);
                        rtc = -1;
                        break;
                    }
                }
                else
                {
                    pp = p;
                }

                if (in_function)
                {
                    int     local_str_idx;

                    if ((tmpline = local_variable_exists (functions + current_function_idx, kw)) > 0)
                    {
                        fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                        rtc = -1;
                        break;
                    }

                    if ((tmpline = global_variable_exists (kw)) > 0)
                    {
                        fprintf (stderr, "warning line %d: variable '%s' shadows global variable '%s' defined in line %d.\n", line, kw, kw, tmpline);
                    }

                    if (arraysize)
                    {
                        local_str_idx = new_local_string_array_variable (functions + current_function_idx, kw, arraysize, line);
                    }
                    else
                    {
                        local_str_idx = new_local_string_variable (functions + current_function_idx, kw, line);
                    }

                    p = pp;

                    if (arraysize == 0)
                    {
                        if (check_initializer (line, p, &pp, -1, -1, local_str_idx, -1, -1, -1, -1, -1) < 0)
                        {
                            rtc = -1;
                            break;
                        }
                    }
                }
                else
                {
                    int global_str_idx;

                    if ((tmpline = global_variable_exists (kw)) > 0)
                    {
                        fprintf (stderr, "error line %d: variable '%s' already defined in line %d.\n", line, kw, tmpline);
                        rtc = -1;
                        break;
                    }

                    if (arraysize)
                    {
                        global_str_idx = new_global_string_array_variable (kw, arraysize, line);
                    }
                    else
                    {
                        global_str_idx = new_global_string_variable (kw, line);
                    }

                    p = pp;

                    if (arraysize == 0)
                    {
                        if (check_initializer (line, p, &pp, -1, -1, -1, -1, -1, global_str_idx, -1, -1) < 0)
                        {
                            rtc = -1;
                            break;
                        }
                    }
                }
                p = pp;
            }
            else if (! ustrcmp (kw, "if"))
            {
                POSTFIX_ELEMENT postfix2[MAX_POSTFIX_DEPTH];
                STATEMENT_STACK stack;
                int             operator;
                unsigned char * restp;
                int             current_postfix_slot;
                int             current_postfix_slot2;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_IF;
                statementp[statements_used].next    = statements_used + 1;

                if (! *p)
                {
                    fprintf (stderr, "error line %d: empty expression.\n", line);
                    rtc = -1;
                    break;
                }

                if ((operator = handle_expression (line, expr, p, WAITING_FOR_COMPARE_OPERATOR_FLAG, &restp)) == EXPRESSION_ERROR)
                {
                    rtc = -1;
                    break;
                }

                if (operator == NO_COMPARE_OPERATOR)
                {
                    fprintf (stderr, "error line %d: no compare operator found.\n", line);
                    rtc = -1;
                    break;
                }

                infix2postfix (postfix, expr->ec);

                current_postfix_slot = new_postfix_slot (postfix);

                if (current_postfix_slot < 0)
                {
                    fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                    rtc = -1;
                    break;
                }

                if (handle_expression (line, expr, restp, NO_FLAG, (unsigned char **) NULL) == EXPRESSION_ERROR)
                {
                    rtc = -1;
                    break;
                }

                infix2postfix (postfix2, expr->ec);

                current_postfix_slot2 = new_postfix_slot (postfix2);

                if (current_postfix_slot2 < 0)
                {
                    fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].st.st_if.postfix_slot1  = current_postfix_slot;
                statementp[statements_used].st.st_if.operator       = operator;
                statementp[statements_used].st.st_if.postfix_slot2  = current_postfix_slot2;
                statementp[statements_used].st.st_if.false_idx      = -1;

                stack.type  = STATEMENT_TYPE_IF;
                stack.idx   = statements_used;

                if (push_statement (&stack) != OK)
                {
                    fprintf (stderr, "error line %d: statement stack overflow.\n", line);
                    rtc = -1;
                    break;
                }

                statements_used++;
                continue;                                                                           // if we call handle_expression(), we musst call continue here
            }
            else if (! ustrcmp (kw, "elseif"))
            {
                POSTFIX_ELEMENT postfix2[MAX_POSTFIX_DEPTH];
                STATEMENT_STACK stack;
                int             operator;
                unsigned char * restp;
                int             current_postfix_slot;
                int             current_postfix_slot2;
                int             i = 1;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                // 1st step: pop old if

                do
                {
                    if (peek_statement (&stack, i) != OK)                                           // only peek
                    {
                        fprintf (stderr, "error line %d: statement stack underflow.\n", line);
                        rtc = -1;
                        break;
                    }

                    if (stack.type == STATEMENT_TYPE_IF)
                    {
                        if (statementp[stack.idx].st.st_if.false_idx < 0)                           // false tree already set by elseif?
                        {                                                                           // no, set it now
                            statementp[stack.idx].st.st_if.false_idx = statements_used;
                        }
                        stack.idx = statements_used;

                        if (poke_statement (&stack, i) != OK)                                       // replace old if with new if
                        {
                            fprintf (stderr, "error line %d: statement stack underflow.\n", line);
                            rtc = -1;
                            break;
                        }
                    }
                    else if (stack.type != STATEMENT_TYPE_ENDIF)
                    {
                        fprintf (stderr, "error line %d: keyword 'else' unexpected.\n", line);
                        rtc = -1;
                        break;
                    }

                    i++;
                } while (stack.type != STATEMENT_TYPE_IF);

                if (rtc < 0)
                {
                    break;
                }

                // 2nd step: handle if:

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_IF;
                statementp[statements_used].next    = statements_used + 1;

                if (! *p)
                {
                    fprintf (stderr, "error line %d: empty expression.\n", line);
                    rtc = -1;
                    break;
                }

                if ((operator = handle_expression (line, expr, p, WAITING_FOR_COMPARE_OPERATOR_FLAG, &restp)) == EXPRESSION_ERROR)
                {
                    rtc = -1;
                    break;
                }

                if (operator == NO_COMPARE_OPERATOR)
                {
                    fprintf (stderr, "error line %d: no compare operator found.\n", line);
                    rtc = -1;
                    break;
                }

                infix2postfix (postfix, expr->ec);

                current_postfix_slot = new_postfix_slot (postfix);

                if (current_postfix_slot < 0)
                {
                    fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                    rtc = -1;
                    break;
                }

                if (handle_expression (line, expr, restp, NO_FLAG, (unsigned char **) NULL) == EXPRESSION_ERROR)
                {
                    rtc = -1;
                    break;
                }

                infix2postfix (postfix2, expr->ec);

                current_postfix_slot2 = new_postfix_slot (postfix2);

                if (current_postfix_slot2 < 0)
                {
                    fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].st.st_if.postfix_slot1  = current_postfix_slot;
                statementp[statements_used].st.st_if.operator       = operator;
                statementp[statements_used].st.st_if.postfix_slot2  = current_postfix_slot2;
                statementp[statements_used].st.st_if.false_idx      = -1;

                // 3rd step: push goto-endif

                stack.type  = STATEMENT_TYPE_ENDIF;
                stack.idx   = statements_used - 1;

                if (push_statement (&stack) != OK)
                {
                    fprintf (stderr, "error line %d: statement stack overflow.\n", line);
                    rtc = -1;
                    break;
                }

                statements_used++;
                continue;                                                                           // if we call handle_expression(), we musst call continue here
            }
            else if (! ustrcmp (kw, "else"))
            {
                STATEMENT_STACK stack;
                int             i = 1;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                do
                {
                    if (peek_statement (&stack, i) != OK)                                           // only peek
                    {
                        fprintf (stderr, "error line %d: statement stack underflow.\n", line);
                        rtc = -1;
                        break;
                    }

                    if (stack.type == STATEMENT_TYPE_IF)
                    {
                        if (statementp[stack.idx].st.st_if.false_idx < 0)                           // false tree already set by elseif?
                        {                                                                           // no, set it now
                            statementp[stack.idx].st.st_if.false_idx = statements_used;
                        }
                    }
                    else if (stack.type != STATEMENT_TYPE_ENDIF)
                    {
                        fprintf (stderr, "error line %d: keyword 'else' unexpected.\n", line);
                        rtc = -1;
                        break;
                    }

                    i++;
                } while (stack.type != STATEMENT_TYPE_IF);

                if (rtc < 0)
                {
                    break;
                }

                stack.type  = STATEMENT_TYPE_ENDIF;
                stack.idx   = statements_used - 1;

                if (push_statement (&stack) != OK)
                {
                    fprintf (stderr, "error line %d: statement stack overflow.\n", line);
                    rtc = -1;
                    break;
                }

                // no statements_used++, don't store 'else' as a statement
            }
            else if (! ustrcmp (kw, "endif"))
            {
                STATEMENT_STACK stack;
                int             if_found = 0;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                do
                {
                    if (peek_statement (&stack, 1) != OK)                                           // only peek here
                    {
                        fprintf (stderr, "error line %d: statement stack underflow.\n", line);
                        rtc = -1;
                        break;
                    }

                    if (stack.type == STATEMENT_TYPE_ENDIF)
                    {
                        pop_statement (&stack);
                        statementp[stack.idx].next = statements_used;
                    }
                    else if (stack.type == STATEMENT_TYPE_IF)
                    {
                        pop_statement (&stack);

                        if (statementp[stack.idx].st.st_if.false_idx < 0)                           // perhaps set by 'else'
                        {
                            statementp[stack.idx].st.st_if.false_idx = statements_used;
                        }
                        if_found = 1;
                    }
                } while (stack.type != STATEMENT_TYPE_IF);

                if (! if_found)
                {
                    fprintf (stderr, "error line %d: keyword 'endif' unexpected.\n", line);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_ENDIF;
                statementp[statements_used].next    = statements_used + 1;

                statements_used++;
            }
            else if (! ustrcmp (kw, "for"))
            {
                STATEMENT_STACK stack;
                int             varidx;
                int             vartype;
                int             current_postfix_slot;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_FOR;
                statementp[statements_used].next    = statements_used + 1;

                p = pp;

                if (check_keyword (kw, line, p, &pp, FALSE) != KEYWORD_IS_IDENTIFIER)                           // for idx = 10 to 20
                {                                                                                               //     ^^^
                    fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                    rtc = -1;
                    break;
                }

                if ((varidx = find_local_int_variable (functions + current_function_idx, kw)) >= 0)
                {
                    functions[current_function_idx].local_int_variables[varidx].set_cnt++;
                    vartype = VARIABLE_TYPE_LOCAL_INT;
                }
                else if ((varidx = find_static_int_variable (functions + current_function_idx, kw)) >= 0)
                {
                    global_int_variables[varidx].set_cnt++;
                    vartype = VARIABLE_TYPE_GLOBAL_INT;
                }
                else if ((varidx = find_global_int_variable (kw)) >= 0)
                {
                    global_int_variables[varidx].set_cnt++;
                    vartype = VARIABLE_TYPE_GLOBAL_INT;
                }
                else if (find_local_const_int_variable (functions + current_function_idx, kw) >= 0 ||
                         find_global_const_int_variable (kw) >= 0 ||
                         find_local_const_string_variable (functions + current_function_idx, kw) >= 0 ||
                         find_global_const_string_variable (kw) >= 0)
                {
                    fprintf (stderr, "error line %d: variable '%s' is of type 'const'.\n", line, kw);
                    rtc = -1;
                    break;
                }
                else if (find_local_byte_variable (functions + current_function_idx, kw)    >= 0 ||
                         find_static_byte_variable (functions + current_function_idx, kw)   >= 0 ||
                         find_global_byte_variable (kw)                                     >= 0 ||
                         find_local_string_variable (functions + current_function_idx, kw)  >= 0 ||
                         find_static_string_variable (functions + current_function_idx, kw) >= 0 ||
                         find_global_string_variable (kw)                                   >= 0)
                {
                    fprintf (stderr, "error line %d: variable '%s' must be of type 'int'.\n", line, kw);
                    rtc = -1;
                    break;
                }
                else
                {
                    fprintf (stderr, "error line %d: variable '%s' not defined.\n", line, kw);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].st.st_for.for_variable_idx   = varidx;
                statementp[statements_used].st.st_for.for_variable_type  = vartype;

                p = pp;

                if (check_keyword (kw, line, p, &pp, FALSE) != KEYWORD_IS_EQUAL)                                // for idx = 10 to 20
                {                                                                                               //         ^
                    fprintf (stderr, "error line %d: syntax error (%d).\n", line, __LINE__);
                    rtc = -1;
                    break;
                }

                p = pp;

                if (handle_expression (line, expr, p, WAITING_FOR_TO_OPERATOR_FLAG, &pp) == EXPRESSION_ERROR)   // for idx = 10 to 20
                {                                                                                               //           ^^
                    rtc = -1;
                    break;
                }

                infix2postfix (postfix, expr->ec);

                current_postfix_slot = new_postfix_slot (postfix);

                if (current_postfix_slot < 0)
                {
                    fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].st.st_for.postfix_slot_start   = current_postfix_slot;

                p = pp;

                if (handle_expression (line, expr, p, WAITING_FOR_STEP_OPERATOR_FLAG, &pp) == EXPRESSION_ERROR) // for idx = 10 to 20
                {                                                                                               //                 ^^
                    rtc = -1;
                    break;
                }

                infix2postfix (postfix, expr->ec);

                current_postfix_slot = new_postfix_slot (postfix);

                if (current_postfix_slot < 0)
                {
                    fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].st.st_for.postfix_slot_stop = current_postfix_slot;

                p = pp;

                if (*p)                                                                                         // optional: step value
                {
                    if (handle_expression (line, expr, p, NO_FLAG, &pp) == EXPRESSION_ERROR)                    // for idx = 10 to 20 step 2
                    {                                                                                           //                         ^
                        rtc = -1;
                        break;
                    }

                    infix2postfix (postfix, expr->ec);

                    current_postfix_slot = new_postfix_slot (postfix);

                    if (current_postfix_slot < 0)
                    {
                        fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                        rtc = -1;
                        break;
                    }

                    statementp[statements_used].st.st_for.postfix_slot_step = current_postfix_slot;

                    p = pp;
                }
                else
                {
                    statementp[statements_used].st.st_for.postfix_slot_step = -1;                               // mark as not used
                }

                stack.type  = STATEMENT_TYPE_FOR;
                stack.idx   = statements_used;

                if (push_statement (&stack) != OK)
                {
                    fprintf (stderr, "error line %d: statement stack overflow.\n", line);
                    rtc = -1;
                    break;
                }

                statements_used++;
                continue;                                               // if we call handle_expression(), we musst call continue here
            }
            else if (! ustrcmp (kw, "endfor"))
            {
                STATEMENT_STACK     stack;
                BREAK_STACK         brstack;
                CONTINUE_STACK      costack;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                if (pop_statement (&stack) != OK)
                {
                    fprintf (stderr, "error line %d: statement stack underflow.\n", line);
                    rtc = -1;
                    break;
                }

                if (stack.type != STATEMENT_TYPE_FOR)
                {
                    fprintf (stderr, "error line %d: keyword 'endfor' unexpected.\n", line);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_ENDFOR;
                statementp[statements_used].next    = statements_used + 1;

                statementp[stack.idx].st.st_for.endfor_idx          = statements_used;
                statementp[statements_used].st.st_endfor.for_idx    = stack.idx;

                while (peek_break (&brstack, 1) == OK)
                {
                    if (brstack.stack_idx == stack.idx)
                    {
                        pop_break (&brstack);
                        statementp[brstack.idx].next = statements_used + 1;
                    }
                    else
                    {
                        break;
                    }
                }

                while (peek_continue (&costack, 1) == OK)
                {
                    if (costack.stack_idx == stack.idx)
                    {
                        pop_continue (&costack);
                        statementp[costack.idx].next = statements_used;                             // continue jumps to 'endfor' to increment variable
                    }
                    else
                    {
                        break;
                    }
                }

                statements_used++;
            }
            else if (! ustrcmp (kw, "while"))
            {
                POSTFIX_ELEMENT postfix2[MAX_POSTFIX_DEPTH];
                STATEMENT_STACK stack;
                int             current_postfix_slot;
                int             current_postfix_slot2;
                int             operator;
                unsigned char * restp;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_WHILE;
                statementp[statements_used].next    = statements_used + 1;

                if (! *p)
                {
                    fprintf (stderr, "error line %d: empty expression.\n", line);
                    rtc = -1;
                    break;
                }

                if ((operator = handle_expression (line, expr, p, WAITING_FOR_COMPARE_OPERATOR_FLAG, &restp)) == EXPRESSION_ERROR)
                {
                    rtc = -1;
                    break;
                }

                if (operator == NO_COMPARE_OPERATOR)
                {
                    fprintf (stderr, "error line %d: no compare operator found.\n", line);
                    rtc = -1;
                    break;
                }

                infix2postfix (postfix, expr->ec);

                current_postfix_slot = new_postfix_slot (postfix);

                if (current_postfix_slot < 0)
                {
                    fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                    rtc = -1;
                    break;
                }

                if (handle_expression (line, expr, restp, NO_FLAG, (unsigned char **) NULL) == EXPRESSION_ERROR)
                {
                    rtc = -1;
                    break;
                }

                infix2postfix (postfix2, expr->ec);

                current_postfix_slot2 = new_postfix_slot (postfix2);

                if (current_postfix_slot2 < 0)
                {
                    fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].st.st_while.postfix_slot1   = current_postfix_slot;
                statementp[statements_used].st.st_while.operator        = operator;
                statementp[statements_used].st.st_while.postfix_slot2   = current_postfix_slot2;

                stack.type  = STATEMENT_TYPE_WHILE;
                stack.idx   = statements_used;

                if (push_statement (&stack) != OK)
                {
                    fprintf (stderr, "error line %d: statement stack overflow.\n", line);
                    rtc = -1;
                    break;
                }

                statements_used++;
                continue;                                               // if we call handle_expression(), we musst call continue here
            }
            else if (! ustrcmp (kw, "endwhile"))
            {
                STATEMENT_STACK     stack;
                BREAK_STACK         brstack;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                if (pop_statement (&stack) != OK)
                {
                    fprintf (stderr, "error line %d: statement stack underflow.\n", line);
                    rtc = -1;
                    break;
                }

                if (stack.type != STATEMENT_TYPE_WHILE)
                {
                    fprintf (stderr, "error line %d: keyword 'endwhile' unexpected.\n", line);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_ENDWHILE;
                statementp[statements_used].next    = statements_used + 1;

                statementp[stack.idx].st.st_while.endwhile_idx          = statements_used;
                statementp[statements_used].st.st_endwhile.while_idx    = stack.idx;

                while (peek_break (&brstack, 1) == OK)
                {
                    if (brstack.stack_idx == stack.idx)
                    {
                        pop_break (&brstack);
                        statementp[brstack.idx].next = statements_used + 1;
                    }
                    else
                    {
                        break;
                    }
                }

                statements_used++;
            }
            else if (! ustrcmp (kw, "loop"))
            {
                STATEMENT_STACK stack;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_LOOP;
                statementp[statements_used].next    = statements_used + 1;

                if (*p && ustrncmp (p, "//", 2))
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected. (%d)\n", line, p, __LINE__);
                    rtc = -1;
                    break;
                }

                stack.type  = STATEMENT_TYPE_LOOP;
                stack.idx   = statements_used;

                if (push_statement (&stack) != OK)
                {
                    fprintf (stderr, "error line %d: statement stack overflow.\n", line);
                    rtc = -1;
                    break;
                }

                statements_used++;
            }
            else if (! ustrcmp (kw, "endloop"))
            {
                STATEMENT_STACK     stack;
                BREAK_STACK         brstack;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                if (pop_statement (&stack) != OK)
                {
                    fprintf (stderr, "error line %d: statement stack underflow.\n", line);
                    rtc = -1;
                    break;
                }

                if (stack.type != STATEMENT_TYPE_LOOP)
                {
                    fprintf (stderr, "error line %d: keyword 'endloop' unexpected.\n", line);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_ENDLOOP;
                statementp[statements_used].next    = statements_used + 1;

                statementp[stack.idx].st.st_loop.endloop_idx        = statements_used;
                statementp[statements_used].st.st_endloop.loop_idx  = stack.idx;

                while (peek_break (&brstack, 1) == OK)
                {
                    if (brstack.stack_idx == stack.idx)
                    {
                        pop_break (&brstack);
                        statementp[brstack.idx].next = statements_used + 1;
                    }
                    else
                    {
                        break;
                    }
                }

                statements_used++;
            }
            else if (! ustrcmp (kw, "return"))
            {
                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_RETURN;
                statementp[statements_used].next    = statements_used + 1;

                if (functions[current_function_idx].return_type == FUNCTION_TYPE_VOID)
                {
                    if (*p)
                    {
                        fprintf (stderr, "error line %d: 'return' with a value, in function returning void.\n", line);
                        rtc = -1;
                        break;
                    }

                    statementp[statements_used].st.st_return.postfix_slot = -1;
                    statements_used++;
                }
                else
                {
                    int current_postfix_slot;

                    if (! *p)
                    {
                        fprintf (stderr, "error line %d: 'return' with no value, in function returning non-void.\n", line);
                        rtc = -1;
                        break;
                    }

                    if (handle_expression (line, expr, p, NO_FLAG, (unsigned char **) NULL) == EXPRESSION_ERROR)
                    {
                        rtc = -1;
                        break;
                    }

                    infix2postfix (postfix, expr->ec);

                    current_postfix_slot = new_postfix_slot (postfix);

                    if (current_postfix_slot < 0)
                    {
                        fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                        rtc = -1;
                        break;
                    }

                    statementp[statements_used].st.st_return.postfix_slot = current_postfix_slot;

                    statements_used++;
                    continue;                                               // if we call handle_expression(), we musst call continue here
                }
            }
            else if (! ustrcmp (kw, "repeat"))
            {
                STATEMENT_STACK stack;
                int current_postfix_slot;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_REPEAT;
                statementp[statements_used].next    = statements_used + 1;

                if (! *p)
                {
                    fprintf (stderr, "error line %d: empty expression.\n", line);
                    rtc = -1;
                    break;
                }

                if (handle_expression (line, expr, p, NO_FLAG, (unsigned char **) NULL) == EXPRESSION_ERROR)
                {
                    rtc = -1;
                    break;
                }

                infix2postfix (postfix, expr->ec);

                current_postfix_slot = new_postfix_slot (postfix);

                if (current_postfix_slot < 0)
                {
                    fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].st.st_repeat.postfix_slot = current_postfix_slot;

                stack.type  = STATEMENT_TYPE_REPEAT;
                stack.idx   = statements_used;

                if (push_statement (&stack) != OK)
                {
                    fprintf (stderr, "error line %d: statement stack overflow.\n", line);
                    rtc = -1;
                    break;
                }

                statements_used++;
                continue;                                               // if we call handle_expression(), we musst call continue here
            }
            else if (! ustrcmp (kw, "endrepeat"))
            {
                STATEMENT_STACK     stack;
                BREAK_STACK         brstack;
                CONTINUE_STACK      costack;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                if (pop_statement (&stack) != OK)
                {
                    fprintf (stderr, "error line %d: statement stack underflow.\n", line);
                    rtc = -1;
                    break;
                }

                if (stack.type != STATEMENT_TYPE_REPEAT)
                {
                    fprintf (stderr, "error line %d: keyword 'endrepeat' unexpected.\n", line);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_ENDREPEAT;
                statementp[statements_used].next    = statements_used + 1;

                statementp[stack.idx].st.st_repeat.endrepeat_idx        = statements_used;
                statementp[statements_used].st.st_endrepeat.repeat_idx  = stack.idx;

                while (peek_break (&brstack, 1) == OK)
                {
                    if (brstack.stack_idx == stack.idx)
                    {
                        pop_break (&brstack);
                        statementp[brstack.idx].next = statements_used + 1;
                    }
                    else
                    {
                        break;
                    }
                }

                while (peek_continue (&costack, 1) == OK)
                {
                    if (costack.stack_idx == stack.idx)
                    {
                        pop_continue (&costack);
                        statementp[costack.idx].next = statements_used;                             // continue jumps to 'endrepeat' to decrement counter
                    }
                    else
                    {
                        break;
                    }
                }

                statements_used++;
            }
            else if (! ustrcmp (kw, "break"))
            {
                STATEMENT_STACK stack;
                BREAK_STACK     brstack;
                int             stack_idx_found = 0;
                int             i;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_BREAK;
                statementp[statements_used].next    = statements_used + 1;

                i = 1;

                while (peek_statement (&stack, i) == OK)
                {
                    if (stack.type == STATEMENT_TYPE_WHILE ||
                        stack.type == STATEMENT_TYPE_LOOP ||
                        stack.type == STATEMENT_TYPE_FOR ||
                        stack.type == STATEMENT_TYPE_REPEAT)
                    {
                        brstack.idx         = statements_used;
                        brstack.stack_idx   = stack.idx;

                        if (push_break (&brstack) != OK)
                        {
                            fprintf (stderr, "error line %d: break stack overflow.\n", line);
                            rtc = -1;
                            break;
                        }

                        stack_idx_found = 1;
                        break;
                    }

                    i++;
                }

                if (rtc < 0)
                {
                    break;
                }

                if (! stack_idx_found)
                {
                    fprintf (stderr, "error line %d: keyword 'break' unexpected.\n", line);
                    rtc = -1;
                    break;
                }

                statements_used++;
            }
            else if (! ustrcmp (kw, "continue"))
            {
                STATEMENT_STACK stack;
                int             stack_idx_found = 0;
                int             i;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                statementp[statements_used].line    = line;
                statementp[statements_used].type    = STATEMENT_TYPE_CONTINUE;

                i = 1;

                while (peek_statement (&stack, i) == OK)
                {
                    if (stack.type == STATEMENT_TYPE_WHILE)
                    {
                        statementp[statements_used].next = stack.idx;
                        stack_idx_found = 1;
                        break;
                    }
                    else if (stack.type == STATEMENT_TYPE_LOOP)
                    {
                        statementp[statements_used].next = statementp[stack.idx].next;
                        stack_idx_found = 1;
                        break;
                    }
                    else if (stack.type == STATEMENT_TYPE_FOR ||                        // we must jump to 'endfor' to increment variable
                        stack.type == STATEMENT_TYPE_REPEAT)                            // we must jump to 'endrepeat' to decrement counter
                    {
                        CONTINUE_STACK  costack;

                        costack.idx         = statements_used;
                        costack.stack_idx   = stack.idx;

                        if (push_continue (&costack) != OK)
                        {
                            fprintf (stderr, "error line %d: continue stack overflow.\n", line);
                            rtc = -1;
                            break;
                        }

                        statementp[statements_used].next = -1;
                        stack_idx_found = 1;
                        break;
                    }

                    i++;
                }

                if (! stack_idx_found)
                {
                    fprintf (stderr, "error line %d: keyword 'continue' unexpected.\n", line);
                    rtc = -1;
                    break;
                }

                statements_used++;
            }
            else if (*kw)
            {
                int r;
                int current_postfix_slot;
                int step;
                int assignment_variable_pslot = -1;

                if (! in_function)
                {
                    fprintf (stderr, "error line %d: keyword '%s' unexpected (%d)\n", line, kw, __LINE__);
                    rtc = -1;
                    break;
                }

                if (*pp == '[')
                {
                    int                     square_bracket_cnt = 1;
                    EXPRESSION_LIST *       sub_expr;
                    POSTFIX_ELEMENT         postfix[MAX_POSTFIX_DEPTH];
                    int                     r;
                    unsigned char *         ppp;

                    ppp = pp + 1;

                    while (*ppp)
                    {
                        if (*ppp == ']')
                        {
                            square_bracket_cnt--;

                            if (square_bracket_cnt == 0)
                            {
                                break;
                            }
                        }
                        else if (*ppp == '[')
                        {
                            square_bracket_cnt++;
                        }
                        ppp++;
                    }

                    if (! *ppp)
                    {
                        fprintf (stderr, "error line %d: no matching ']' found.\n", line);
                        rtc = -1;
                    }

                    *ppp = '\0';

                    sub_expr = new_expression_list (__FILE__, __LINE__);

                    if ((r = handle_expression (line, sub_expr, pp, 0, (unsigned char **) NULL)) == EXPRESSION_ERROR)
                    {
                        rtc = -1;
                        break;
                    }

                    infix2postfix (postfix, sub_expr->ec);

                    free_expression_list (__FILE__, __LINE__, sub_expr);

                    assignment_variable_pslot = new_postfix_slot (postfix);

                    pp = skip_blanks (ppp + 1);
                }

                if (*pp == '=')
                {
                    int arraysize = 0;

                    ustrcpy (assignment_variable, kw);

                    if ((assignment_variable_idx
                            = find_local_int_variable (functions + current_function_idx, (unsigned char *) assignment_variable)) >= 0)
                    {
                        functions[current_function_idx].local_int_variables[assignment_variable_idx].set_cnt++;
                        assignment_variable_type = VARIABLE_TYPE_LOCAL_INT;
                    }
                    else if ((assignment_variable_idx
                            = find_local_int_array_variable (functions + current_function_idx, (unsigned char *) assignment_variable)) >= 0)
                    {
                        functions[current_function_idx].local_int_array_variables[assignment_variable_idx].set_cnt++;
                        arraysize = functions[current_function_idx].local_int_array_variables[assignment_variable_idx].arraysize;
                        assignment_variable_type = VARIABLE_TYPE_LOCAL_INT_ARRAY;
                    }
                    else if ((assignment_variable_idx
                            = find_local_byte_variable (functions + current_function_idx, (unsigned char *) assignment_variable)) >= 0)
                    {
                        functions[current_function_idx].local_byte_variables[assignment_variable_idx].set_cnt++;
                        assignment_variable_type = VARIABLE_TYPE_LOCAL_BYTE;
                    }
                    else if ((assignment_variable_idx
                            = find_local_byte_array_variable (functions + current_function_idx, (unsigned char *) assignment_variable)) >= 0)
                    {
                        functions[current_function_idx].local_byte_array_variables[assignment_variable_idx].set_cnt++;
                        arraysize = functions[current_function_idx].local_byte_array_variables[assignment_variable_idx].arraysize;
                        assignment_variable_type = VARIABLE_TYPE_LOCAL_BYTE_ARRAY;
                    }
                    else if ((assignment_variable_idx
                            = find_local_string_variable (functions + current_function_idx, (unsigned char *) assignment_variable)) >= 0)
                    {
                        functions[current_function_idx].local_string_variables[assignment_variable_idx].set_cnt++;
                        assignment_variable_type = VARIABLE_TYPE_LOCAL_STRING;
                    }
                    else if ((assignment_variable_idx
                            = find_local_string_array_variable (functions + current_function_idx, (unsigned char *) assignment_variable)) >= 0)
                    {
                        functions[current_function_idx].local_string_array_variables[assignment_variable_idx].set_cnt++;
                        arraysize = functions[current_function_idx].local_string_array_variables[assignment_variable_idx].arraysize;
                        assignment_variable_type = VARIABLE_TYPE_LOCAL_STRING_ARRAY;
                    }
                    else if ((assignment_variable_idx
                            = find_static_int_variable (functions + current_function_idx, (unsigned char *) assignment_variable)) >= 0)
                    {
                        global_int_variables[assignment_variable_idx].set_cnt++;
                        assignment_variable_type = VARIABLE_TYPE_GLOBAL_INT;
                    }
                    else if ((assignment_variable_idx
                            = find_static_int_array_variable (functions + current_function_idx, (unsigned char *) assignment_variable)) >= 0)
                    {
                        global_int_array_variables[assignment_variable_idx].set_cnt++;
                        arraysize = global_int_array_variables[assignment_variable_idx].arraysize;
                        assignment_variable_type = VARIABLE_TYPE_GLOBAL_INT_ARRAY;
                    }
                    else if ((assignment_variable_idx
                            = find_static_byte_variable (functions + current_function_idx, (unsigned char *) assignment_variable)) >= 0)
                    {
                        global_byte_variables[assignment_variable_idx].set_cnt++;
                        assignment_variable_type = VARIABLE_TYPE_GLOBAL_BYTE;
                    }
                    else if ((assignment_variable_idx
                            = find_static_byte_array_variable (functions + current_function_idx, (unsigned char *) assignment_variable)) >= 0)
                    {
                        global_byte_array_variables[assignment_variable_idx].set_cnt++;
                        arraysize = global_byte_array_variables[assignment_variable_idx].arraysize;
                        assignment_variable_type = VARIABLE_TYPE_GLOBAL_BYTE_ARRAY;
                    }
                    else if ((assignment_variable_idx
                            = find_static_string_variable (functions + current_function_idx, (unsigned char *) assignment_variable)) >= 0)
                    {
                        global_string_variables[assignment_variable_idx].set_cnt++;
                        assignment_variable_type = VARIABLE_TYPE_GLOBAL_STRING;
                    }
                    else if ((assignment_variable_idx
                            = find_static_string_array_variable (functions + current_function_idx, (unsigned char *) assignment_variable)) >= 0)
                    {
                        global_string_array_variables[assignment_variable_idx].set_cnt++;
                        arraysize = global_string_array_variables[assignment_variable_idx].arraysize;
                        assignment_variable_type = VARIABLE_TYPE_GLOBAL_STRING_ARRAY;
                    }
                    else if (find_local_const_int_variable (functions + current_function_idx, (unsigned char *) assignment_variable) >= 0 ||
                             find_global_const_int_variable ((unsigned char *) assignment_variable) >= 0 ||
                             find_local_const_string_variable (functions + current_function_idx, (unsigned char *) assignment_variable) >= 0 ||
                             find_global_const_string_variable ((unsigned char *) assignment_variable) >= 0)
                    {
                        fprintf (stderr, "error line %d: variable '%s' is of type 'const'.\n", line, assignment_variable);
                        rtc = -1;
                        break;
                    }
                    else if ((assignment_variable_idx = find_global_int_variable ((unsigned char *) assignment_variable)) >= 0)
                    {
                        global_int_variables[assignment_variable_idx].set_cnt++;
                        assignment_variable_type = VARIABLE_TYPE_GLOBAL_INT;
                    }
                    else if ((assignment_variable_idx = find_global_int_array_variable ((unsigned char *) assignment_variable)) >= 0)
                    {
                        global_int_array_variables[assignment_variable_idx].set_cnt++;
                        arraysize = global_int_array_variables[assignment_variable_idx].arraysize;
                        assignment_variable_type = VARIABLE_TYPE_GLOBAL_INT_ARRAY;
                    }
                    else if ((assignment_variable_idx = find_global_byte_variable ((unsigned char *) assignment_variable)) >= 0)
                    {
                        global_byte_variables[assignment_variable_idx].set_cnt++;
                        assignment_variable_type = VARIABLE_TYPE_GLOBAL_BYTE;
                    }
                    else if ((assignment_variable_idx = find_global_byte_array_variable ((unsigned char *) assignment_variable)) >= 0)
                    {
                        global_byte_array_variables[assignment_variable_idx].set_cnt++;
                        arraysize = global_byte_array_variables[assignment_variable_idx].arraysize;
                        assignment_variable_type = VARIABLE_TYPE_GLOBAL_BYTE_ARRAY;
                    }
                    else if ((assignment_variable_idx = find_global_string_variable ((unsigned char *) assignment_variable)) >= 0)
                    {
                        global_string_variables[assignment_variable_idx].set_cnt++;
                        assignment_variable_type = VARIABLE_TYPE_GLOBAL_STRING;
                    }
                    else if ((assignment_variable_idx = find_global_string_array_variable ((unsigned char *) assignment_variable)) >= 0)
                    {
                        global_string_array_variables[assignment_variable_idx].set_cnt++;
                        arraysize = global_string_array_variables[assignment_variable_idx].arraysize;
                        assignment_variable_type = VARIABLE_TYPE_GLOBAL_STRING_ARRAY;
                    }
                    else
                    {
                        fprintf (stderr, "error line %d: variable '%s' not defined.\n", line, assignment_variable);
                        rtc = -1;
                        break;
                    }

                    if (assignment_variable_pslot >= 0 && arraysize == 0)
                    {
                        fprintf (stderr, "error line %d: variable '%s' is not an array variable.\n", line, kw);
                        rtc = -1;
                        break;
                    }

                    if (assignment_variable_pslot < 0 && arraysize > 0)
                    {
                        fprintf (stderr, "error line %d: variable '%s' is an array variable.\n", line, kw);
                        rtc = -1;
                        break;
                    }

                    p = pp;
                    check_keyword (kw, line, p, &pp, FALSE);                            // skip '='
                    p = pp;
                }
                else
                {
                    assignment_variable_type = VARIABLE_TYPE_LOCAL_INT;                 // todo, must be initialized, even if not used
                    p = buf;
                }

                if (! *p)
                {
                    fprintf (stderr, "error line %d: empty expression.\n", line);
                    rtc = -1;
                    break;
                }

                last_undefined_function_idx = -1;                                       // reset it only here, handle_expression may call itself recursively

                if ((r = handle_expression (line, expr, p, NO_FLAG, (unsigned char **) NULL)) == EXPRESSION_ERROR)
                {
                    rtc = -1;
                    break;
                }

                if (assignment_variable_idx >= 0 && last_undefined_function_idx >= 0)
                {
                    undefined_functions[last_undefined_function_idx].needs_return_value = 1;
                }

                if (assignment_variable_idx >= 0 && r == FUNCTION_RETURNING_VOID)
                {
                    if (last_void_function_type == EXPRESSION_CONTENT_TYPE_INTERN_FUNCTION)
                    {
                        fprintf (stderr, "error line %d: function '%s' returns void.\n", line, function_list[last_void_function_idx].name);
                    }
                    else if (last_void_function_type == EXPRESSION_CONTENT_TYPE_EXTERN_FUNCTION)
                    {
                        fprintf (stderr, "error line %d: function '%s' defined in line %d returns void.\n", line,
                                functions[last_void_function_idx].name, functions[last_void_function_idx].line);
                    }
                    else // if (last_void_function_type == EXPRESSION_CONTENT_TYPE_UNDEFINED_FUNCTION)
                    {
                        fprintf (stderr, "internal error %s line %d.\n", __FILE__, __LINE__);
                    }
                    rtc = -1;
                    break;
                }

                infix2postfix (postfix, expr->ec);

                statementp[statements_used].line    = line;
                statementp[statements_used].next    = statements_used + 1;

                if (! statement_calls_function (postfix))
                {
                    int n;

                    if (assignment_variable_idx < 0)
                    {
                        fprintf (stderr, "error line %d: statement takes no effect.\n", line);
                        rtc = -1;
                        break;
                    }

                    if ((n = statement_uses_variable (assignment_variable_idx, assignment_variable_type, postfix)) > 0)
                    {
                        switch (assignment_variable_type)
                        {
                            case VARIABLE_TYPE_LOCAL_INT:
                                functions[current_function_idx].local_int_variables[assignment_variable_idx].used_cnt -= n;
                                break;
                            case VARIABLE_TYPE_LOCAL_INT_ARRAY:
                                functions[current_function_idx].local_int_array_variables[assignment_variable_idx].used_cnt -= n;
                                break;
                            case VARIABLE_TYPE_LOCAL_BYTE:
                                functions[current_function_idx].local_byte_variables[assignment_variable_idx].used_cnt -= n;
                                break;
                            case VARIABLE_TYPE_LOCAL_BYTE_ARRAY:
                                functions[current_function_idx].local_byte_array_variables[assignment_variable_idx].used_cnt -= n;
                                break;
                            case VARIABLE_TYPE_LOCAL_STRING:
                                functions[current_function_idx].local_string_variables[assignment_variable_idx].used_cnt -= n;
                                break;
                            case VARIABLE_TYPE_LOCAL_STRING_ARRAY:
                                functions[current_function_idx].local_string_array_variables[assignment_variable_idx].used_cnt -= n;
                                break;
                            case VARIABLE_TYPE_GLOBAL_INT:
                                global_int_variables[assignment_variable_idx].used_cnt -= n;
                                break;
                            case VARIABLE_TYPE_GLOBAL_INT_ARRAY:
                                global_int_array_variables[assignment_variable_idx].used_cnt -= n;
                                break;
                            case VARIABLE_TYPE_GLOBAL_BYTE:
                                global_byte_variables[assignment_variable_idx].used_cnt -= n;
                                break;
                            case VARIABLE_TYPE_GLOBAL_BYTE_ARRAY:
                                global_byte_array_variables[assignment_variable_idx].used_cnt -= n;
                                break;
                            case VARIABLE_TYPE_GLOBAL_STRING:
                                global_string_variables[assignment_variable_idx].used_cnt -= n;
                                break;
                            case VARIABLE_TYPE_GLOBAL_STRING_ARRAY:
                                global_string_array_variables[assignment_variable_idx].used_cnt -= n;
                                break;
                        }
                    }
                }

                if ((step = statement_is_increment_variable (assignment_variable_idx, assignment_variable_type, postfix)) != 0)
                {
                    statementp[statements_used].type = STATEMENT_TYPE_INCREMENT;

                    statementp[statements_used].st.st_increment.variable_idx    = assignment_variable_idx;
                    statementp[statements_used].st.st_increment.variable_type   = assignment_variable_type;
                    statementp[statements_used].st.st_increment.step            = step;
                }
                else
                {
                    statementp[statements_used].type    = STATEMENT_TYPE_INTERN_FUNCTION;
                    statementp[statements_used].next    = statements_used + 1;

                    statementp[statements_used].st.st_intern_function.assignment_variable_idx   = assignment_variable_idx;
                    statementp[statements_used].st.st_intern_function.assignment_variable_type  = assignment_variable_type;
                    statementp[statements_used].st.st_intern_function.assignment_variable_pslot = assignment_variable_pslot;

                    current_postfix_slot = new_postfix_slot (postfix);

                    if (current_postfix_slot < 0)
                    {
                        fprintf (stderr, "error line %d: no postfix slots available.\n", line);
                        rtc = -1;
                        break;
                    }

                    statementp[statements_used].st.st_intern_function.postfix_slot = current_postfix_slot;
                }

                statements_used++;
                continue;                                               // if we call handle_expression(), we musst call continue here
            }

            t = check_keyword (kw, line, p, &pp, FALSE);

            if (t != KEYWORD_IS_EMPTY)
            {
                fprintf (stderr, "error line %d: keyword '%s' unexpected. (%d)\n", line, kw, __LINE__);
                rtc = -1;
                break;
            }
        }

        free_expression_list (__FILE__, __LINE__, expr);

        fclose (fp);
    }
    else
    {
#ifdef unix
        perror (in);
#else
        fprintf (stderr, "%s: cannot open\n", in);
#endif
        rtc = -1;
    }

    if (rtc >= 0)
    {
        if (in_function)
        {
            fprintf (stderr, "error line %d: missing 'endfunction' at end of file.\n", line);
            rtc = -1;
        }
        else if (statement_stack_depth > 0)
        {
            STATEMENT_STACK stack;

            peek_statement (&stack, 1);

            switch (stack.type)
            {
                case STATEMENT_TYPE_IF:
                    fprintf (stderr, "error line %d: missing 'endif', 'if' or 'elseif' in line %d\n", line, statementp[stack.idx].line);
                    break;
                case STATEMENT_TYPE_ENDIF:
                    fprintf (stderr, "error line %d: missing 'if' or 'elseif' in line %d\n", line, statementp[stack.idx].line);
                    break;
                case STATEMENT_TYPE_WHILE:
                    fprintf (stderr, "error line %d: missing 'endwhile', 'while' in line %d\n", line, statementp[stack.idx].line);
                    break;
                case STATEMENT_TYPE_LOOP:
                    fprintf (stderr, "error line %d: missing 'endloop', 'loop' in line %d\n", line, statementp[stack.idx].line);
                    break;
                case STATEMENT_TYPE_REPEAT:
                    fprintf (stderr, "error line %d: missing 'endrepeat', 'repeat' in line %d\n", line, statementp[stack.idx].line);
                    break;
                default:
                    fprintf (stderr, "internal error line %d: missing 'endxxxx', 'xxxx' in line %d\n", line, statementp[stack.idx].line);
                    break;
            }

            rtc = -1;
        }

        if (rtc >= 0)
        {
            check_const_variables ();
            check_global_variables ();

            if (check_undefined_functions () < 0)
            {
                rtc = -1;
            }
            else
            {
                check_functions ();
            }
        }
    }

    if (verbose)
    {
        int     idx;
        int     opt_cnt = 0;
        int     max_slots_used;

        for (idx = 0; idx < statements_used; idx++)
        {
            if (statementp[idx].type == STATEMENT_TYPE_INCREMENT)
            {
                opt_cnt++;
            }
        }

        fprintf (stderr, "statements optimized:  %3d / %3d\n", opt_cnt, statements_used);

        int siz;
        int sum = 0;

        siz = statements_allocated * sizeof (STATEMENT);
        fprintf (stderr, "statements:            %3d / %3d = %5u bytes\n", statements_used, statements_allocated, siz);
        sum += siz;

        siz = size_functions ();
        fprintf (stderr, "functions:             %3d / %3d = %5u bytes\n", functions_used, functions_allocated, siz);
        sum += siz;

        siz = size_undefined_functions ();
        fprintf (stderr, "undefined functions:   %3d / %3d = %5u bytes\n", undefined_functions_used, undefined_functions_allocated, siz);
        sum += siz;

        siz = size_fipslots ();
        fprintf (stderr, "fipslots:              %3d / %3d = %5u bytes\n", fipslots_used, fipslots_allocated, siz);
        sum += siz;

        siz = size_string_constants ();
        fprintf (stderr, "string constants:      %3d / %3d = %5u bytes\n", string_constants_used, string_constants_allocated, siz);
        sum += siz;

        siz = const_int_variables_allocated * sizeof (VARIABLE);
        fprintf (stderr, "const  int variables:  %3d / %3d = %5u bytes\n", const_int_variables_used, const_int_variables_allocated, siz);
        sum += siz;

        siz = global_int_variables_allocated * sizeof (VARIABLE);
        fprintf (stderr, "global int variables:  %3d / %3d = %5u bytes\n", global_int_variables_used, global_int_variables_allocated, siz);
        sum += siz;

        siz = global_int_array_variables_allocated * sizeof (ARRAY_VARIABLE);
        fprintf (stderr, "global int arrays:     %3d / %3d = %5u bytes\n", global_int_array_variables_used, global_int_array_variables_allocated, siz);

        siz = global_byte_variables_allocated * sizeof (VARIABLE);
        fprintf (stderr, "global byte variables: %3d / %3d = %5u bytes\n", global_byte_variables_used, global_byte_variables_allocated, siz);
        sum += siz;

        siz = global_byte_array_variables_allocated * sizeof (ARRAY_VARIABLE);
        fprintf (stderr, "global byte arrays:    %3d / %3d = %5u bytes\n", global_byte_array_variables_used, global_byte_array_variables_allocated, siz);
        sum += siz;

        siz = const_string_variables_allocated * sizeof (VARIABLE);
        fprintf (stderr, "const  str variables:  %3d / %3d = %5u bytes\n", const_string_variables_used, const_string_variables_allocated, siz);
        sum += siz;

        siz = global_string_variables_allocated * sizeof (VARIABLE);
        fprintf (stderr, "global str variables:  %3d / %3d = %5u bytes\n", global_string_variables_used, global_string_variables_allocated, siz);
        sum += siz;

        siz = global_string_array_variables_allocated * sizeof (ARRAY_VARIABLE);
        fprintf (stderr, "global str arrays:     %3d / %3d = %5u bytes\n", global_string_array_variables_used, global_string_array_variables_allocated, siz);
        sum += siz;

        siz = size_postfix_slots ();
        fprintf (stderr, "postfix_slots:         %3d / %3d = %5u bytes\n", postfix_slots_used, postfix_slots_allocated, siz);
        sum += siz;

        fprintf (stderr, "                                   -----------\n");
        fprintf (stderr, "sum:                               %5u bytes\n", sum);

        max_slots_used = alloc_max_slots_used ();

        if (max_slots_used >= 0)
        {
            fprintf (stderr, "max alloc slots used:  %3d\n", max_slots_used);
            fprintf (stderr, "max alloc memory used: %lu bytes\n", alloc_max_memory_used ());
        }
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * free_statements - free all allocated statements
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
free_statements (void)
{
    alloc_free (__FILE__, __LINE__, statementp);

    statementp                  = 0;
    statements_used             = 0;
    statements_allocated        = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * write all statements into object file
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
dump_statements (FILE * fp)
{
    int     idx;
    fprintf (fp, "%d\n", statements_used);

    for (idx = 0; idx < statements_used; idx++)
    {
        fprintf (fp, "%d %d %d ", statementp[idx].line, statementp[idx].type, statementp[idx].next);

        switch (statementp[idx].type)
        {
            case STATEMENT_TYPE_INCREMENT:
            {
                STATEMENT_INCREMENT * stp = &(statementp[idx].st.st_increment);

                fprintf (fp, "%d %d %d", stp->variable_idx, stp->variable_type, stp->step);
                break;
            }

            case STATEMENT_TYPE_INTERN_FUNCTION:
            {
                STATEMENT_INTERN_FUNCTION * stp = &(statementp[idx].st.st_intern_function);

                fprintf (fp, "%d %d %d %d", stp->assignment_variable_idx, stp->assignment_variable_type, stp->assignment_variable_pslot, stp->postfix_slot);
                break;
            }

            case STATEMENT_TYPE_IF:
            {
                STATEMENT_IF * stp = &(statementp[idx].st.st_if);
                fprintf (fp, "%d %d %d %d", stp->postfix_slot1, stp->operator, stp->postfix_slot2, stp->false_idx);
                break;
            }

            case STATEMENT_TYPE_ENDIF:
            {
                // nothing to do
                break;
            }

            case STATEMENT_TYPE_WHILE:
            {
                STATEMENT_WHILE * stp = &(statementp[idx].st.st_while);
                fprintf (fp, "%d %d %d %d", stp->postfix_slot1, stp->operator, stp->postfix_slot2, stp->endwhile_idx);
                break;
            }

            case STATEMENT_TYPE_ENDWHILE:
            {
                STATEMENT_ENDWHILE * stp = &(statementp[idx].st.st_endwhile);
                fprintf (fp, "%d", stp->while_idx);
                break;
            }

            case STATEMENT_TYPE_FOR:
            {
                STATEMENT_FOR * stp = &(statementp[idx].st.st_for);
                fprintf (fp, "%d %d %d %d %d %d", stp->for_variable_idx, stp->for_variable_type,
                        stp->postfix_slot_start, stp->postfix_slot_stop, stp->postfix_slot_step, stp->endfor_idx);
                break;
            }

            case STATEMENT_TYPE_ENDFOR:
            {
                STATEMENT_ENDFOR * stp = &(statementp[idx].st.st_endfor);
                fprintf (fp, "%d", stp->for_idx);
                break;
            }

            case STATEMENT_TYPE_REPEAT:
            {
                STATEMENT_REPEAT * stp = &(statementp[idx].st.st_repeat);
                fprintf (fp, "%d %d", stp->postfix_slot, stp->endrepeat_idx);
                break;
            }

            case STATEMENT_TYPE_ENDREPEAT:
            {
                STATEMENT_ENDREPEAT * stp = &(statementp[idx].st.st_endrepeat);
                fprintf (fp, "%d", stp->repeat_idx);
                break;
            }

            case STATEMENT_TYPE_LOOP:
            {
                STATEMENT_LOOP * stp = &(statementp[idx].st.st_loop);
                fprintf (fp, "%d", stp->endloop_idx);
                break;
            }

            case STATEMENT_TYPE_ENDLOOP:
            {
                STATEMENT_ENDLOOP * stp = &(statementp[idx].st.st_endloop);
                fprintf (fp, "%d", stp->loop_idx);
                break;                                                                                      // nothing to do
            }

            case STATEMENT_TYPE_BREAK:
            {
                break;                                                                                      // nothing to do
            }

            case STATEMENT_TYPE_CONTINUE:
            {
                break;                                                                                      // nothing to do
            }

            case STATEMENT_TYPE_RETURN:
            {
                STATEMENT_RETURN * stp = &(statementp[idx].st.st_return);
                fprintf (fp, "%d", stp->postfix_slot);
                break;
            }

            default:
            {
                fprintf (stderr, "error line %d: unhandled statement %d\n", statementp[idx].line, idx);
                return ERR;
            }
        }
        putc ('\n', fp);
    }

    return OK;
}


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * write all string constants into object file
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
dump_string_constants (FILE * fp)
{
    int i;

    fprintf (fp, "%d\n", string_constants_used);

    for (i = 0; i < string_constants_used; i++)
    {
        if (string_constants[i])
        {
            fprintf (fp, "%s\n", string_constants[i]);
        }
        else
        {
            fputc ('\n', fp);                                                               // empty (unused) string constant
        }
    }

    return OK;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * write all global variables into object file
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
dump_global_variables (FILE * fp)
{
    int idx;

    fprintf (fp, "%d\n", global_int_variables_used);

    for (idx = 0; idx < global_int_variables_used; idx++)
    {
        fprintf (fp, "%d\n", global_int_variables[idx].v.int_value);
    }

    fprintf (fp, "%d\n", global_byte_variables_used);

    for (idx = 0; idx < global_byte_variables_used; idx++)
    {
        fprintf (fp, "%d\n", global_byte_variables[idx].v.int_value);
    }

    fprintf (fp, "%d\n", global_string_variables_used);

    for (idx = 0; idx < global_string_variables_used; idx++)
    {
        if (global_string_variables[idx].v.str_value)
        {
            fprintf (fp, "%s\n", global_string_variables[idx].v.str_value);
        }
        else
        {
            fprintf (fp, "%s\n", "");
        }
    }

    return OK;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * write all global array variables into object file
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
dump_global_array_variables (FILE * fp)
{
    int i;

    fprintf (fp, "%d\n", global_int_array_variables_used);

    for (i = 0; i < global_int_array_variables_used; i++)
    {
        fprintf (fp, "%d\n", global_int_array_variables[i].arraysize);
    }

    fprintf (fp, "%d\n", global_byte_array_variables_used);

    for (i = 0; i < global_byte_array_variables_used; i++)
    {
        fprintf (fp, "%d\n", global_byte_array_variables[i].arraysize);
    }

    fprintf (fp, "%d\n", global_string_array_variables_used);

    for (i = 0; i < global_string_array_variables_used; i++)
    {
        fprintf (fp, "%d\n", global_string_array_variables[i].arraysize);
    }

    return OK;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * write all functions into object file
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
dump_functions (FILE * fp)
{
    int     i;
    int     j;
    int     main_function_idx = -1;

    fprintf (fp, "%d\n", functions_used);

    for (i = 0; i < functions_used; i++)
    {
        fprintf (fp, "%d %d %d ", functions[i].first_statement_idx, functions[i].return_type, functions[i].argc);

        for (j = 0; j < functions[i].argc; j++)
        {
            if (functions[i].argtypes[j] == ARGUMENT_TYPE_INT)
            {
                fprintf (fp, "%c", 'i');
            }
            else if (functions[i].argtypes[j] == ARGUMENT_TYPE_BYTE)
            {
                fprintf (fp, "%c", 'b');
            }
            else if (functions[i].argtypes[j] == ARGUMENT_TYPE_STRING)
            {
                fprintf (fp, "%c", 's');
            }
            else
            {
                fprintf (stderr, "error line %d: invalid argument type %d in function '%s', argument #%d\n",
                        functions[i].line, functions[i].argtypes[j], functions[i].name, j);
                return ERR;
            }

            fprintf (fp, "%d ", functions[i].argvars[j]);
        }
        putc ('\n', fp);

        fprintf (fp, "%d %d %d\n", functions[i].local_int_variables_used, functions[i].local_byte_variables_used, functions[i].local_string_variables_used);

        fprintf (fp, "%d\n", functions[i].local_int_array_variables_used);

        for (j = 0; j < functions[i].local_int_array_variables_used; j++)
        {
            fprintf (fp, "%d\n", functions[i].local_int_array_variables[j].arraysize);
        }

        fprintf (fp, "%d\n", functions[i].local_byte_array_variables_used);

        for (j = 0; j < functions[i].local_byte_array_variables_used; j++)
        {
            fprintf (fp, "%d\n", functions[i].local_byte_array_variables[j].arraysize);
        }

        fprintf (fp, "%d\n", functions[i].local_string_array_variables_used);

        for (j = 0; j < functions[i].local_string_array_variables_used; j++)
        {
            fprintf (fp, "%d\n", functions[i].local_string_array_variables[j].arraysize);
        }

        if (! strcmp (functions[i].name, "main"))
        {
            main_function_idx = i;

            if (functions[i].return_type != FUNCTION_TYPE_VOID)
            {
                fprintf (stderr, "error: main must be defined as function returning void.\n");
                return ERR;
            }
        }
    }

    if (main_function_idx >= 0)
    {
        fprintf (fp, "%d\n", main_function_idx);
    }
    else
    {
        fprintf (stderr, "error: no main function found.\n");
        return ERR;
    }
    return OK;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * dump all data into object file
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
dump_all (char * out, int verbose)
{
    FILE *  fp;
    int     rtc = ERR;

    fp = fopen (out, "w");

    if (fp)
    {
        if (dump_statements (fp)                == OK &&
            dump_postfix_slots (fp, verbose)    == OK &&
            dump_fipslots (fp)                  == OK &&
            dump_string_constants (fp)          == OK &&
            dump_global_variables (fp)          == OK &&
            dump_global_array_variables (fp)    == OK &&
            dump_functions (fp)                 == OK)
        {
            rtc = OK;
        }
        fclose (fp);
    }
    else
    {
#ifdef unix
        perror (out);
#else
        fprintf (stderr, "%s: cannot open\n", out);
#endif
    }

    return rtc;
}

#if defined (WIN32)
/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * open serial port
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static HANDLE
open_port (wchar_t * comport)
{
    HANDLE hdl;

    hdl = CreateFile (comport,                          // Pointer to the name of the port
                        GENERIC_READ | GENERIC_WRITE,   // Access (read-write) mode
                        0,                              // Share mode
                        NULL,                           // Pointer to the security attribute
                        OPEN_EXISTING,                  // How to open the serial port
                        0,                              // Port attributes
                        NULL);                          // Handle to port with attribute
    return (hdl);
}

#elif defined (unix)

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * set nodelay on stdin
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
set_nodelay (int fd, int flag)
{
    int     fl;

    if ((fl = fcntl (fd, F_GETFL, 0)) >= 0)
    {
        if (flag)
        {
            fl |= O_NDELAY;
        }
        else
        {
            fl &= ~O_NDELAY;
        }
        (void) fcntl (fd, F_SETFL, fl);
        nodelay_set = flag;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * open port - open tty port
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
open_port (const char * comport)
{
    int hdl;

    hdl = open (comport, O_RDWR);
    return (hdl);
}

#endif

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * configure serial port
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#if defined (WIN32)

static int
init_port (HANDLE hdl, int baudrate)
{
    DCB                     PortDCB;
    COMMTIMEOUTS            timeouts;

    PortDCB.DCBlength = sizeof (DCB);                   // initialize DCBlength member.
    GetCommState (hdl, &PortDCB);                       // get default port setting information.

    // Change the DCB structure settings.
    PortDCB.BaudRate            = baudrate;             // Current baud
    PortDCB.fBinary             = TRUE;                 // Binary mode; no EOF check
    PortDCB.fParity             = TRUE;                 // Enable parity checking
    PortDCB.fOutxCtsFlow        = FALSE;                // No CTS output flow control
    PortDCB.fOutxDsrFlow        = FALSE;                // No DSR output flow control
    PortDCB.fDtrControl         = DTR_CONTROL_ENABLE;   // DTR flow control type
    PortDCB.fDsrSensitivity     = FALSE;                // DSR sensitivity
    PortDCB.fTXContinueOnXoff   = TRUE;                 // XOFF continues Tx
    PortDCB.fOutX               = FALSE;                // No XON/XOFF out flow control
    PortDCB.fInX                = FALSE;                // No XON/XOFF in flow control
    PortDCB.fErrorChar          = FALSE;                // Disable error replacement
    PortDCB.fNull               = FALSE;                // Disable null stripping
    PortDCB.fRtsControl         = RTS_CONTROL_ENABLE;   // RTS flow control
    PortDCB.fAbortOnError       = FALSE;                // Do not abort reads/writes on error
    PortDCB.ByteSize            = 8;                    // Number of bits/byte, 4-8
    PortDCB.Parity              = NOPARITY;             // 0-4=no,odd,even,mark,space
    PortDCB.StopBits            = ONESTOPBIT;           // 1 stop bit: ONESTOPBIT, 1.5 stop bits: ONE5STOPBITS, 2 stop bits: TWOSTOPBITS

    if (! SetCommState (hdl, &PortDCB))                 // configure port
    {
        DWORD dwError;
        dwError = GetLastError ();
        fprintf (stderr, "unable to configure the serial port");
        return (FALSE);
    }

    timeouts.ReadIntervalTimeout            = 1;
    timeouts.ReadTotalTimeoutMultiplier     = 1;
    timeouts.ReadTotalTimeoutConstant       = 1;
    timeouts.WriteTotalTimeoutMultiplier    = 1;
    timeouts.WriteTotalTimeoutConstant      = 1;

    if (! SetCommTimeouts (hdl, &timeouts))
    {
        fprintf (stderr, "cannot set timeouts\n");
    }

    return (TRUE);
}

#elif defined (unix)

static int
init_port (int hdl, int baudrate)
{
    speed_t     speed = B115200;

    if (tcgetattr (hdl, &savetty) >= 0)
    {
        tty_saved = TRUE;
        savehdl = hdl;

        tty = savetty;

        switch (baudrate)
        {
            case 9600:      speed = B9600;      break;
            case 19200:     speed = B19200;     break;
            case 38400:     speed = B38400;     break;
            case 57600:     speed = B57600;     break;
            case 115200:    speed = B115200;    break;
            default:
                fprintf (stderr, "baudrate %d not supported, using 115200 bd.\n", baudrate);
                break;
        }

        cfsetospeed(&tty, speed);
        cfsetispeed(&tty, speed);

        tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
        tty.c_oflag &= ~OPOST;
        tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
        tty.c_cflag &= ~(CSIZE | PARENB);
        tty.c_cflag |= CS8;

        tty.c_cc[VMIN] = 1;
        tty.c_cc[VTIME] = 5;                                                        // 0: block, else: timeout in 1/10 seconds

        tty.c_cflag &= ~CSTOPB;                                                     // 1 stop bit
        tty.c_cflag &= ~CRTSCTS;                                                    // no HW flow control
        tty.c_cflag |= CLOCAL;                                                      // ignore modem control lines
        tty.c_cflag |= CREAD;                                                       // enable receiver

        if (tcsetattr (hdl, TCSADRAIN, &tty) < 0)
        {
            tcsetattr (hdl, TCSADRAIN, &savetty);
            fprintf (stderr, "failed to set termio attr.\n");
            return 0;
        }
    }

    return (TRUE);
}

#endif

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * close serial port
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#if defined (WIN32)

static void
close_port (HANDLE hdl)
{
    CloseHandle (hdl);
}

#elif defined (unix)

static void
close_port (int hdl)
{
    tcsetattr (hdl, TCSADRAIN, &savetty);
    close (hdl);
}

#endif

#define MAX_LINE_LEN    256

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * upload data
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#if defined (WIN32)

static int
upload_data (HANDLE hdl, FILE * fp)
{
    char                    buf[MAX_LINE_LEN];
    unsigned char           answer[1];
    int                     i;
    int                     len;
    DWORD                   n_read;
    DWORD                   n_written;
    DWORD                   dwStatus;
    int                     rtc = ERR;

    while (fgets (buf, MAX_LINE_LEN, fp))
    {
        len = strlen (buf);

        for (i = 0; i < len; i++)
        {
            if (buf[i] == '\r' || buf[i] == '\n')
            {
                buf[i++] = '\r';
                buf[i++] = '\n';
                buf[i] = '\0';
                len = i;
            }
        }

        if (! WriteFile (hdl, buf, len, &n_written, NULL) || n_written != len)
        {
            fprintf (stderr, "COM write error\n");
            return rtc;
        }

        WaitCommEvent (hdl, &dwStatus, 0);                              // wait for event

        do
        {
            if (ReadFile(hdl, answer, 1, &n_read, NULL))
            {
                if (n_read > 0 && answer[0] != ACK)
                {
                    fprintf (stderr, "upload failed, n_read = %d answer = 0x%02x\n", n_read, n_read >= 1 ? answer[0] : 0);
                    return rtc;
                }
            }
        } while (n_read == 0);
    }

    while (ReadFile (hdl, answer, 1, &n_read, NULL))
    {
        if (_kbhit ())
        {
            int key = _getch ();

            if (key == 27)  // ESC
            {
                break;
            }
        }

        if (n_read)
        {
            if ((answer[0] == '\r' || answer[0] == '\n') || (answer[0] >= 32 && answer[0] <= 127))
            {
                fputc (answer[0], stdout);
            }
            else
            {
                printf ("<%02x>", answer[0]);
            }
        }
    }

    rtc = OK;
    return rtc;
}

#elif defined (unix)

static int
upload_data (int hdl, FILE * fp)
{
    char                    buf[MAX_LINE_LEN];
    unsigned char           answer[1];
    int                     i;
    int                     len;
    int                     n_read;
    int                     n_written;
    int                     rtc = ERR;

    while (fgets (buf, MAX_LINE_LEN, fp))
    {
        len = strlen (buf);

        for (i = 0; i < len; i++)
        {
            if (buf[i] == '\r' || buf[i] == '\n')
            {
                buf[i++] = '\r';
                buf[i++] = '\n';
                buf[i] = '\0';
                len = i;
                break;
            }
        }

        if ((n_written = write (hdl, buf, len)) < 0 || n_written != len)
        {
            fprintf (stderr, "TTY write error: len=%d n_written=%d\n", len, n_written);
            return rtc;
        }

        do
        {
            if ((n_read = read (hdl, answer, 1)) >= 0)
            {
                if (n_read > 0 && answer[0] != ACK)
                {
                    fprintf (stderr, "upload failed, n_read = %d answer = 0x%02x\n", n_read, n_read >= 1 ? answer[0] : 0);
                    return rtc;
                }
            }
        } while (n_read == 0);
    }

    set_nodelay (fileno(stdin), TRUE);

    while ((n_read = read (hdl, answer, 1)) >= 0)
    {
        int key = getchar ();

        if (key == 27)  // ESC
        {
            break;
        }

        if (n_read)
        {
            if ((answer[0] == '\r' || answer[0] == '\n') || (answer[0] >= 32 && answer[0] <= 127))
            {
                fputc (answer[0], stdout);
            }
            else
            {
                printf ("<%02x>", answer[0]);
            }
        }
    }

    set_nodelay (fileno(stdin), FALSE);

    rtc = OK;
    return rtc;
}

#endif

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * upload file
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#if defined (WIN32)

static int
upload_file (wchar_t * comport, char * fname)
{
    FILE *  fp;
    HANDLE  hdl;
    int             rtc = ERR;

    fp = fopen (fname, "r");

    if (! fp)
    {
        fprintf (stderr, "cannot open file '%s'.\n", fname);
        return rtc;
    }

    hdl = open_port (comport);

    if (hdl == (HANDLE) INVALID_HANDLE_VALUE)
    {
        fprintf (stderr, "cannot open com port '%ls'.\n", comport);
        fclose (fp);
        return rtc;
    }

    if (! init_port (hdl, 115200))
    {
        close_port (hdl);
        fclose (fp);
        return rtc;
    }

    rtc = upload_data (hdl, fp);
    close_port (hdl);
    fclose (fp);
    return rtc;
}

#elif defined (unix)

static int
upload_file (const char * comport, const char * fname)
{
    FILE *  fp;
    int     hdl;
    int     rtc = ERR;

    fp = fopen (fname, "r");

    if (! fp)
    {
        perror (fname);
        return rtc;
    }

    hdl = open_port (comport);

    if (hdl < 0)
    {
        perror (comport);
        fclose (fp);
        return rtc;
    }

    if (! init_port (hdl, 115200))
    {
        close_port (hdl);
        fclose (fp);
        return rtc;
    }

    rtc = upload_data (hdl, fp);
    close_port (hdl);
    fclose (fp);
    return rtc;
}

#endif

#if defined (unix)
/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * signal handler
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
sighandler (int sig)
{
    fprintf (stderr, "got signal %d, exit.\n", sig);

    if (nodelay_set)
    {
        set_nodelay (fileno(stdin), FALSE);
    }

    if (tty_saved)
    {
        tcsetattr (savehdl, TCSADRAIN, &savetty);
    }

    exit (1);
}

#endif

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * print usage
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
usage (const char * pgm)
{
#if defined (unix) || defined (WIN32)           // no upload on STM32
    fprintf (stderr, "usage: %s [-v] [-u comport] file\n", pgm);
#else
    fprintf (stderr, "usage: %s [-v] file\n", pgm);
#endif
    return;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * call_setjmp - small wrapper function for setjmp to avoid clobber warnings of compiler
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int  setjmp_rtc;

static int
call_setjmp (void)
{
    if ((setjmp_rtc = setjmp (env)) == 0)
    {
        return 0;
    }
    return setjmp_rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * main
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#if defined (unix) || defined (WIN32)
#define cmd_nicc    main
#endif

int
cmd_nicc (int argc, const char ** argv)
{
    int             verbose = 0;
    char            outfile[256];
#if defined (WIN32)
    wchar_t         comport[256];
    int             do_upload = FALSE;
#elif defined (unix)
    const char *    comport = NULL;
    int             do_upload = FALSE;
#endif
    int             rtc = EXIT_FAILURE;

    while (argc > 2)
    {
        if (!strcmp (argv[1], "-v"))
        {
            verbose = 1;
            argc--;
            argv++;
        }
        else if (!strcmp (argv[1], "-vv"))
        {
            verbose = 2;
            argc--;
            argv++;
        }
#if defined (unix) || defined (WIN32)
        else if (argc >= 3 && !strcmp (argv[1], "-u"))
        {
#if defined (WIN32)
            char device[32];
#endif
            argc--;
            argv++;
#if defined (WIN32)
            sprintf (device, "\\\\.\\%s", argv[1]);
            mbstowcs(comport, device, 256);
#elif defined (unix)
            comport = argv[1];
#endif
            argc--;
            argv++;
            do_upload = TRUE;
        }
#endif // unix or windows
        else
        {
            break;
        }
    }

    if (argc == 2)
    {
        if (call_setjmp () == 0)
        {
            if (nicc (argv[1], verbose) == OK)
            {
                sprintf (outfile, "%sic", argv[1]);

                if (dump_all (outfile, verbose) == OK)
                {
#if defined (unix) || defined (WIN32)
                    if (do_upload)
                    {
#if defined (unix)
                        signal (SIGHUP, sighandler);
                        signal (SIGINT, sighandler);
                        signal (SIGTERM, sighandler);
#endif
                        upload_file (comport, outfile);
                    }
#endif // unix or windows
                    rtc = EXIT_SUCCESS;
                }
            }
        }

        if (setjmp_rtc)                                                                             // NOT else!
        {
            rtc = setjmp_rtc;
            // fprintf (stderr, "EXIT with code %d\n", rtc);
        }

        expr_free_postfix_slots ();
        free_fipslots ();

        free_string_constants ();
        free_undefined_functions ();
        free_functions ();

        free_const_int_variables ();
        free_global_int_variables ();
        free_global_int_array_variables ();

        free_global_byte_variables ();
        free_global_byte_array_variables ();

        free_const_string_variables ();
        free_global_string_variables ();
        free_global_string_array_variables ();

        free_statements ();

        reset_globals ();
        alloc_list ();
        alloc_free_holes ();
    }
    else
    {
        usage (argv[0]);
    }

    return rtc;
}
