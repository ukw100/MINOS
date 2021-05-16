/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nic.c - nic interpreter
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
#ifdef unix
#include <signal.h>
#endif

#include "nicstrings.h"
#include "nic-common.h"
#include "functions.h"
#include "nic-base.h"
#include "alloc.h"
#include "nic.h"

static FILE * fp;

typedef struct
{
    int     result;
    int     result_type;
    int     result_postfix_slot;
} RESULT;

typedef struct
{
    int *       values;
    int         arraysize;
} INT_ARRAY_VARIABLE;

typedef struct
{
    uint8_t *   values;
    int         arraysize;
} BYTE_ARRAY_VARIABLE;

typedef struct
{
    int *       slots;
    int         arraysize;
} STRING_ARRAY_VARIABLE;

typedef struct
{
    int                             first_statement_idx;
    int                             return_type;
    int                             argc;
    int *                           argvars;
    int *                           argtypes;
    int                             local_int_variables_used;
    int *                           local_int_variables;

    int                             local_int_array_variables_used;
    int *                           local_int_arraysizes;
    int **                          local_int_array_variables;

    int                             local_byte_variables_used;
    uint8_t *                       local_byte_variables;

    int                             local_byte_array_variables_used;
    int *                           local_byte_arraysizes;
    uint8_t **                      local_byte_array_variables;

    int                             local_string_variables_used;
    int *                           local_string_variables;

    int                             local_string_array_variables_used;
    int *                           local_string_arraysizes;
    int **                          local_string_array_variables;
} FUNCTION;

#define ACK                         0x06
#define NACK                        0x15

static int                          main_function_idx;
static int                          main_argc;
static const char **                main_argv;

STATEMENT *                         statementp;
static int                          statements_used = 0;

static POSTFIX_ELEMENT **           postfix_slots;
static int *                        postfix_depth;
static int *                        postfix_hint;
static int                          postfix_slots_used = 0;

static FIP_RUN **                   fip_run_slots;
static int                          fipslots_used = 0;

#define LOCAL_VARIABLE_STACK_ALLOC_GRANULARITY  32

static int *                        local_int_variable_stack;
static int                          local_int_variable_stack_used;
static int                          local_int_variable_stack_allocated;

static uint8_t *                    local_byte_variable_stack;
static int                          local_byte_variable_stack_used;
static int                          local_byte_variable_stack_allocated;

static int *                        local_string_variable_stack;
static int                          local_string_variable_stack_used;
static int                          local_string_variable_stack_allocated;

static int                          evaluate_postfix_slot (int, RESULT *);

#if 0
#define MAX_FUNCTION_STACK_DEPTH    256
static int                          function_stack[MAX_FUNCTION_STACK_DEPTH];
static int                          function_stack_ptr = 0;
#endif

static int                          functions_used = 0;
static FUNCTION *                   functions;
static FUNCTION *                   current_function;

static int *                        global_int_variables;
static int                          global_int_variables_used;

static INT_ARRAY_VARIABLE *         global_int_array_variables;
static int                          global_int_array_variables_used;

static uint8_t *                    global_byte_variables;
static int                          global_byte_variables_used;

static BYTE_ARRAY_VARIABLE *        global_byte_array_variables;
static int                          global_byte_array_variables_used;

static int *                        global_string_variables;
static int                          global_string_variables_used;

static STRING_ARRAY_VARIABLE *      global_string_array_variables;
static int                          global_string_array_variables_used;

static int                          (**func)(FIP_RUN *);

#ifdef unix
static int interrupted;

static void
mysighandler (int sig)
{
    if (sig == SIGINT)
    {
        interrupted = 1;
    }
}

static int
console_interrupted (void)
{
    return interrupted;
}
#endif // unix

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 *  push a value/type on stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
push (EXPRESSION_STACK * stackp, int value, int type, int postfix_slot)
{
#if 0
    if (stackp->stack_pointer < MAX_EXPR_EXPRESSION_STACK_DEPTH)
    {
        stackp->stack[stackp->stack_pointer] = value;
        stackp->type[stackp->stack_pointer] = type;
        stackp->postfix_slot[stackp->stack_pointer] = postfix_slot;
        stackp->stack_pointer++;
    }
    else
    {
        fprintf (stderr, "expression too complex, stack size exceeded\n");
        exit (1);
    }
#else
    stackp->stack[stackp->stack_pointer] = value;
    stackp->type[stackp->stack_pointer] = type;
    stackp->postfix_slot[stackp->stack_pointer] = postfix_slot;
    stackp->stack_pointer++;
#endif
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 *  pop a value/type from stack
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
pop (EXPRESSION_STACK * stackp, RESULT * rp)
{
#if 0
    if (stackp->stack_pointer == 0)
    {
        fprintf (stderr, "fatal: run pop: stackpointer at bottom\n");
        exit(1);
    }
#endif

    stackp->stack_pointer--;

    rp->result              = stackp->stack[stackp->stack_pointer];
    rp->result_type         = stackp->type[stackp->stack_pointer];
    rp->result_postfix_slot = stackp->postfix_slot[stackp->stack_pointer];
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * get result as integer result
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
get_result_int (RESULT * rp)
{
    int rtc = rp->result;

    if (rp->result_type != OPERAND_INT_CONSTANT)                // it's faster if you check the most possible case with simple if() instead of switch()
    {
        RESULT r_idx;
        int     result_idx;

        switch (rp->result_type)
        {
            case OPERAND_STRING_CONSTANT:
                rtc = uatoi (stringslots[rp->result]->str);
                break;
            case OPERAND_TEMP_STRING_CONSTANT:
                if (tmp_stringslots[rp->result]->flags & STRING_FLAG_TEMP_ACTIVE)
                {
                    tmp_stringslots[rp->result]->flags &= ~STRING_FLAG_TEMP_ACTIVE;
                }
                else
                {
                    fprintf (stderr, "internal error in get_result_int(): temp string [%d] '%s' is not marked as temp string (%d)\n",
                                rp->result, tmp_stringslots[rp->result]->str, __LINE__);
                }
                rtc = uatoi (tmp_stringslots[rp->result]->str);
                break;
            case OPERAND_LOCAL_STRING_VARIABLE:
                rtc = uatoi (stringslots[current_function->local_string_variables[rp->result]]->str);
                break;
            case OPERAND_LOCAL_STRING_ARRAY_VARIABLE:
                evaluate_postfix_slot (rp->result_postfix_slot, &r_idx);
                result_idx = get_result_int (&r_idx);

                if (result_idx >= 0 && result_idx < current_function->local_string_arraysizes[rp->result])
                {
                    rtc = uatoi (stringslots[current_function->local_string_array_variables[rp->result][result_idx]]->str);
                }
                else
                {
                    fprintf (stderr, "fatal error: index %d of local string array[%d] is out of range (%d)\n",
                                    result_idx, current_function->local_string_arraysizes[rp->result], __LINE__);
                    exit (1);
                }
                break;
            case OPERAND_GLOBAL_STRING_VARIABLE:
                rtc = uatoi (stringslots[global_string_variables[rp->result]]->str);
                break;
            case OPERAND_GLOBAL_STRING_ARRAY_VARIABLE:
                evaluate_postfix_slot (rp->result_postfix_slot, &r_idx);
                result_idx = get_result_int (&r_idx);

                if (result_idx >= 0 && result_idx < global_string_array_variables[rp->result].arraysize)
                {
                    rtc = uatoi (stringslots[global_string_array_variables[rp->result].slots[result_idx]]->str);
                }
                else
                {
                    fprintf (stderr, "fatal error: index %d of global string array[%d] is out of range (%d)\n",
                                    result_idx, global_string_array_variables[rp->result].arraysize, __LINE__);
                    exit (1);
                }
                break;
            case OPERAND_GLOBAL_BYTE_ARRAY_PTR:
                rtc = global_byte_array_variables[rp->result].arraysize;
                break;
            case OPERAND_LOCAL_BYTE_ARRAY_PTR:
                rtc = current_function->local_byte_arraysizes[rp->result];
                break;
            default:
                fprintf (stderr, "internal error in get_result_int(): unknown result_type = %d (%d)\n", rp->result_type, __LINE__);
                break;
        }
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * get argument (type as original)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
get_argument (FIP_RUN * fip, int argi, unsigned char ** resultstrp, int * resultp)
{
    RESULT          r;
    RESULT          r_idx;
    int             result_idx;
    int             rtc = RESULT_UNKNOWN;

    evaluate_postfix_slot (fip->postfix_slotp[argi], &r);

    switch (r.result_type)
    {
        case OPERAND_INT_CONSTANT:                                                  // argument is of type int
            *resultp = r.result;
            rtc = RESULT_INT;
            break;
        case OPERAND_STRING_CONSTANT:                                               // else: argument is of type string
            *resultstrp = stringslots[r.result]->str;
            rtc = RESULT_CSTRING;
            break;
        case OPERAND_TEMP_STRING_CONSTANT:
            *resultstrp = tmp_stringslots[r.result]->str;

            if (tmp_stringslots[r.result]->flags & STRING_FLAG_TEMP_ACTIVE)
            {
                tmp_stringslots[r.result]->flags &= ~STRING_FLAG_TEMP_ACTIVE;
            }
            else
            {
                fprintf (stderr, "internal runtime error: temp string [%d] '%s' is not marked as temp string (%s %d)\n",
                            r.result, tmp_stringslots[r.result]->str, __FILE__, __LINE__);
            }

            rtc = RESULT_CSTRING;
            break;
        case OPERAND_LOCAL_STRING_VARIABLE:
            *resultstrp = stringslots[current_function->local_string_variables[r.result]]->str;
            rtc = RESULT_CSTRING;
            break;
        case OPERAND_LOCAL_STRING_ARRAY_VARIABLE:
            evaluate_postfix_slot (r.result_postfix_slot, &r_idx);
            result_idx = get_result_int (&r_idx);

            if (result_idx >= 0 && result_idx < current_function->local_string_arraysizes[r.result])
            {
                *resultstrp = stringslots[current_function->local_string_array_variables[r.result][result_idx]]->str;
            }
            else
            {
                fprintf (stderr, "fatal error: index %d of local string array[%d] is out of range (%d)\n",
                                result_idx, current_function->local_string_arraysizes[r.result], __LINE__);
                exit (1);
            }
            rtc = RESULT_CSTRING;
            break;
        case OPERAND_GLOBAL_STRING_VARIABLE:
            *resultstrp = stringslots[global_string_variables[r.result]]->str;
            rtc = RESULT_CSTRING;
            break;
        case OPERAND_GLOBAL_STRING_ARRAY_VARIABLE:
            evaluate_postfix_slot (r.result_postfix_slot, &r_idx);
            result_idx = get_result_int (&r_idx);

            if (result_idx >= 0 && result_idx < global_string_array_variables[r.result].arraysize)
            {
                *resultstrp = stringslots[global_string_array_variables[r.result].slots[result_idx]]->str;
            }
            else
            {
                fprintf (stderr, "fatal error: index %d of global string array[%d] is out of range (%d)\n",
                                result_idx, global_string_array_variables[r.result].arraysize, __LINE__);
                exit (1);
            }
            rtc = RESULT_CSTRING;
            break;
        case OPERAND_GLOBAL_BYTE_ARRAY_PTR:
            *resultstrp = global_byte_array_variables[r.result].values;
            *resultp    = global_byte_array_variables[r.result].arraysize;
            rtc = RESULT_BYTE_ARRAY;
            break;
        case OPERAND_LOCAL_BYTE_ARRAY_PTR:
            *resultstrp = current_function->local_byte_array_variables[r.result];
            *resultp    = current_function->local_byte_arraysizes[r.result];
            rtc = RESULT_BYTE_ARRAY;
            break;
        default:
            fprintf (stderr, "internal error in get_argument(): unknown result_type = %d (%d)\n", r.result_type, __LINE__);
            break;
    }
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * get argument (type as int)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
get_argument_int (FIP_RUN * fip, int argi)
{
    RESULT  r;
    int     result;

    evaluate_postfix_slot (fip->postfix_slotp[argi], &r);
    result = get_result_int (&r);
    return result;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * get argument (type as byte)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
get_argument_byte (FIP_RUN * fip, int argi)
{
    RESULT  r;
    int     result;

    evaluate_postfix_slot (fip->postfix_slotp[argi], &r);
    result = get_result_int (&r);
    return (unsigned char) result;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * get argument (type as byte)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
uint8_t *
get_argument_byte_ptr (FIP_RUN * fip, int argi)
{
    RESULT      r;
    uint8_t *   rtc = (uint8_t *) NULL;

    evaluate_postfix_slot (fip->postfix_slotp[argi], &r);

    if (r.result_type == OPERAND_LOCAL_BYTE_ARRAY_PTR)
    {
        rtc = current_function->local_byte_array_variables[r.result];
    }
    else if (r.result_type == OPERAND_GLOBAL_BYTE_ARRAY_PTR)
    {
        rtc = global_byte_array_variables[r.result].values;
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * get argument (type as string)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
unsigned char *
get_argument_string (FIP_RUN * fip, int argi)
{
    static unsigned char    tmp[32];
    unsigned char *         str = (unsigned char *) NULL;
    RESULT                  r;
    RESULT                  r_idx;
    int                     result_idx;

    evaluate_postfix_slot (fip->postfix_slotp[argi], &r);

    switch (r.result_type)
    {
        case OPERAND_INT_CONSTANT:
            sprintf ((char *) tmp, "%d", r.result);
            str = tmp;
            break;
        case OPERAND_STRING_CONSTANT:
            str = stringslots[r.result]->str;
            break;
        case OPERAND_TEMP_STRING_CONSTANT:
            if (tmp_stringslots[r.result]->flags & STRING_FLAG_TEMP_ACTIVE)
            {
                tmp_stringslots[r.result]->flags &= ~STRING_FLAG_TEMP_ACTIVE;
            }
            else
            {
                fprintf (stderr, "internal runtime error: temp string [%d] '%s' is not marked as temp string (%s %d)\n",
                            r.result, tmp_stringslots[r.result]->str, __FILE__, __LINE__);
            }
            str = tmp_stringslots[r.result]->str;
            break;
        case OPERAND_LOCAL_STRING_VARIABLE:
            str = stringslots[current_function->local_string_variables[r.result]]->str;
            break;
        case OPERAND_LOCAL_STRING_ARRAY_VARIABLE:
            evaluate_postfix_slot (r.result_postfix_slot, &r_idx);
            result_idx = get_result_int (&r_idx);

            if (result_idx >= 0 && result_idx < current_function->local_string_arraysizes[r.result])
            {
                str = stringslots[current_function->local_string_array_variables[r.result][result_idx]]->str;
            }
            else
            {
                fprintf (stderr, "fatal error: index %d of local string array[%d] is out of range (%d)\n",
                                result_idx, current_function->local_string_arraysizes[r.result], __LINE__);
                exit (1);
            }
            break;
        case OPERAND_GLOBAL_STRING_VARIABLE:
            str = stringslots[global_string_variables[r.result]]->str;
            break;
        case OPERAND_GLOBAL_STRING_ARRAY_VARIABLE:
            evaluate_postfix_slot (r.result_postfix_slot, &r_idx);
            result_idx = get_result_int (&r_idx);

            if (result_idx >= 0 && result_idx < global_string_array_variables[r.result].arraysize)
            {
                str = stringslots[global_string_array_variables[r.result].slots[result_idx]]->str;
            }
            else
            {
                fprintf (stderr, "fatal error: index %d of global string array[%d] is out of range (%d)\n",
                                result_idx, global_string_array_variables[r.result].arraysize, __LINE__);
                exit (1);
            }
            break;
        default:
            str = (unsigned char *) "ERROR";
            fprintf (stderr, "internal error in get_argument_string(): unknown result_type = %d (%d)\n", r.result_type, __LINE__);
            break;
    }

    return str;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * print postfix on stdout
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#if 0
static void
print_postfix_slot (int slot)
{
    POSTFIX_ELEMENT *   p = postfix_slots[slot];
    int                 depth = postfix_depth[slot];
    int                 idx = 0;

    printf("slot=%d depth=%d ", slot, depth);

    for (idx = 0; idx < depth; idx++)
    {
        if (p[idx].type == OPERATOR)
        {
            printf("o%c", p[idx].value);
        }
        else if (p[idx].type == OPERAND_INT_CONSTANT)
        {
            printf("c%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_LOCAL_INT_VARIABLE)
        {
            printf("v%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_LOCAL_BYTE_VARIABLE)
        {
            printf("b%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_LOCAL_BYTE_ARRAY_VARIABLE)
        {
            printf("ab%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_GLOBAL_INT_VARIABLE)
        {
            printf("V%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_GLOBAL_INT_ARRAY_VARIABLE)
        {
            printf("aV%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_GLOBAL_BYTE_VARIABLE)
        {
            printf("B%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_GLOBAL_BYTE_ARRAY_VARIABLE)
        {
            printf("aB%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_TEMP_STRING_CONSTANT)
        {
            printf("T%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_STRING_CONSTANT)
        {
            printf("C%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_LOCAL_STRING_VARIABLE)
        {
            printf("s%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_LOCAL_STRING_ARRAY_VARIABLE)
        {
            printf("as%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_GLOBAL_STRING_VARIABLE)
        {
            printf("S%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_GLOBAL_STRING_ARRAY_VARIABLE)
        {
            printf("aS%d", p[idx].value);
        }
        else if (p[idx].type == OPERAND_INTERN_FUNCTION)
        {
            printf("f%d", fip_run_slots[p[idx].value]->func_idx);
        }
        else if (p[idx].type == OPERAND_EXTERN_FUNCTION)
        {
            printf("F%d", fip_run_slots[p[idx].value]->func_idx);
        }
        else
        {
            printf ("?");
        }
    }

    putchar ('\n');
}
#endif

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * calc () - calculate
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
calc (int operator, int val1, int val2)
{
    switch (operator)
    {
        case '+':   return val1 + val2;
        case '-':   return val1 - val2;
        case '*':   return val1 * val2;
        case '/':   return val1 / val2;
        case '%':   return val1 % val2;
        case '<':   return (unsigned) val1 << (unsigned) val2;
        case '>':   return (unsigned) val1 >> (unsigned) val2;
        case '&':   return (unsigned) val1 & (unsigned) val2;
        case '|':   return (unsigned) val1 | (unsigned) val2;
        case '^':   return (unsigned) val1 ^ (unsigned) val2;
    }
    return -1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 *  evaluate postfix slot
 *
 *  possible values of *result_typep:
 *
 *  OPERAND_INT_CONSTANT
 *  OPERAND_STRING_CONSTANT
 *  OPERAND_TEMP_STRING_CONSTANT
 *  OPERAND_LOCAL_STRING_VARIABLE
 *  OPERAND_LOCAL_STRING_ARRAY_VARIABLE
 *  OPERAND_GLOBAL_STRING_VARIABLE
 *  OPERAND_GLOBAL_STRING_ARRAY_VARIABLE
 *
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
evaluate_postfix (POSTFIX_ELEMENT * p, int depth, RESULT * rp)
{
    EXPRESSION_STACK    stack;
    EXPRESSION_STACK *  stackp  = &stack;
    int                 result_type = OPERAND_INT_CONSTANT;
    int                 result = 0;
    int                 idx = 0;
    RESULT              r1;
    RESULT              r2;
    RESULT              r_idx;
    int                 result_idx;

    stackp->stack_pointer = 0;

    /* iterate over postfix expression... */
    for (idx = 0; idx < depth; idx++)
    {
        switch (p[idx].type)
        {
            case OPERAND_INT_CONSTANT:
            case OPERAND_STRING_CONSTANT:
            case OPERAND_LOCAL_STRING_VARIABLE:
            case OPERAND_LOCAL_STRING_ARRAY_VARIABLE:
            case OPERAND_GLOBAL_STRING_VARIABLE:
            case OPERAND_GLOBAL_STRING_ARRAY_VARIABLE:
                push(stackp, p[idx].value, p[idx].type, p[idx].postfix_slot);
                break;
            case OPERAND_LOCAL_INT_VARIABLE:
                push(stackp, current_function->local_int_variables[p[idx].value], OPERAND_INT_CONSTANT, -1);
                break;
            case OPERAND_LOCAL_INT_ARRAY_VARIABLE:
                evaluate_postfix_slot (p[idx].postfix_slot, &r_idx);
                result_idx = get_result_int (&r_idx);

                if (result_idx >= 0 && result_idx < current_function->local_int_arraysizes[p[idx].value])
                {
                    push(stackp, current_function->local_int_array_variables[p[idx].value][result_idx], OPERAND_INT_CONSTANT, -1);
                }
                else
                {
                    fprintf (stderr, "fatal error: index %d of local int array[%d] is out of range (%d)\n",
                                    result_idx, current_function->local_int_arraysizes[p[idx].value], __LINE__);
                    exit (1);
                }
                break;
            case OPERAND_GLOBAL_INT_VARIABLE:
                push(stackp, global_int_variables[p[idx].value], OPERAND_INT_CONSTANT, -1);
                break;
            case OPERAND_GLOBAL_INT_ARRAY_VARIABLE:
                evaluate_postfix_slot (p[idx].postfix_slot, &r_idx);
                result_idx = get_result_int (&r_idx);

                if (result_idx >= 0 && result_idx < global_int_array_variables[p[idx].value].arraysize)
                {
                    push(stackp, global_int_array_variables[p[idx].value].values[result_idx], OPERAND_INT_CONSTANT, -1);
                }
                else
                {
                    fprintf (stderr, "fatal error: index %d of global int array[%d] is out of range (%d)\n",
                                    result_idx, current_function->local_int_arraysizes[p[idx].value], __LINE__);
                    exit (1);
                }
                break;
            case OPERAND_LOCAL_BYTE_VARIABLE:
                push(stackp, current_function->local_byte_variables[p[idx].value], OPERAND_INT_CONSTANT, -1);
                break;
            case OPERAND_LOCAL_BYTE_ARRAY_VARIABLE:
                if (p[idx].postfix_slot < 0)                                        // it's a pointer to array
                {
                    push(stackp, p[idx].value, OPERAND_LOCAL_BYTE_ARRAY_PTR, -1);
                }
                else
                {
                    evaluate_postfix_slot (p[idx].postfix_slot, &r_idx);
                    result_idx = get_result_int (&r_idx);

                    if (result_idx >= 0 && result_idx < current_function->local_byte_arraysizes[p[idx].value])
                    {
                        push(stackp, current_function->local_byte_array_variables[p[idx].value][result_idx], OPERAND_INT_CONSTANT, -1);
                    }
                    else
                    {
                        fprintf (stderr, "fatal error: index %d of local byte array[%d] is out of range (%d)\n",
                                        result_idx, current_function->local_byte_arraysizes[p[idx].value], __LINE__);
                        exit (1);
                    }
                }
                break;
            case OPERAND_GLOBAL_BYTE_VARIABLE:
                push(stackp, global_byte_variables[p[idx].value], OPERAND_INT_CONSTANT, -1);
                break;
            case OPERAND_GLOBAL_BYTE_ARRAY_VARIABLE:
                if (p[idx].postfix_slot < 0)                                        // it's a pointer to array
                {
                    push(stackp, p[idx].value, OPERAND_GLOBAL_BYTE_ARRAY_PTR, -1);
                }
                else
                {
                    evaluate_postfix_slot (p[idx].postfix_slot, &r_idx);
                    result_idx = get_result_int (&r_idx);

                    if (result_idx >= 0 && result_idx < global_byte_array_variables[p[idx].value].arraysize)
                    {
                        push(stackp, global_byte_array_variables[p[idx].value].values[result_idx], OPERAND_INT_CONSTANT, -1);
                    }
                    else
                    {
                        fprintf (stderr, "fatal error: index %d of global byte array[%d] is out of range (%d)\n",
                                        result_idx, current_function->local_byte_arraysizes[p[idx].value], __LINE__);
                        exit (1);
                    }
                }
                break;
            case OPERAND_INTERN_FUNCTION:
            {
                int         i = p[idx].value;
                FIP_RUN *   fip = fip_run_slots[i];
                int         func_idx = fip->func_idx;
                int         return_type;

                return_type = (*func[func_idx])(fip);

                switch (return_type)
                {
                    case FUNCTION_TYPE_INT:
                        push(stackp, fip->reti, OPERAND_INT_CONSTANT, -1);
                        break;
                    case FUNCTION_TYPE_STRING:
                        push(stackp, fip->reti, OPERAND_TEMP_STRING_CONSTANT, -1);
                        break;
                    default: // FUNCTION_TYPE_VOID
                        push(stackp, 0, OPERAND_INT_CONSTANT, -1);                          // push a dummy
                        break;
                }

                // TODO: Saemtliche TEMP_STRINGS in Argumenten freigeben, wenn die Funktion _nicht_ get_argumentxxx() aufruft.
                // kill_temp_strings (POSTFIX_ELEMENT * p)
                break;
            } // OPERAND_INTERN_FUNCTION
            case OPERAND_EXTERN_FUNCTION:
            {
                FUNCTION *  save_current_function;
                FIP_RUN *   fip         = fip_run_slots[p[idx].value];
                int         func_idx    = fip->func_idx;
                FUNCTION *  fp          = functions + func_idx;

                if (fp->argc != fip->argc)
                {
                    fprintf (stderr, "internal runtime error: func_idx = %d, fp->argc = %d, fip->argc = %d\n", func_idx, fp->argc, fip->argc);
                    exit (1);
                }

                save_current_function = current_function;

                if (nici (func_idx, fip) < 0)
                {
                    return -1;
                }

                current_function = save_current_function;

                switch (functions[func_idx].return_type)
                {
                    case FUNCTION_TYPE_INT:
                        push(stackp, fip->reti, OPERAND_INT_CONSTANT, -1);
                        break;
                    case FUNCTION_TYPE_STRING:
                        push(stackp, fip->reti, OPERAND_TEMP_STRING_CONSTANT, -1);
                        break;
                    default: // FUNCTION_TYPE_VOID
                        push(stackp, 0, OPERAND_INT_CONSTANT, -1);                          // push a dummy
                        break;
                }
                break;
            } // OPERAND_EXTERN_FUNCTION
            default:
            {
                pop (stackp, &r2);
                pop (stackp, &r1);

                if (p[idx].value == ':')
                {
                    char        tmp[32];
                    STRING *    t;

                    result_type = OPERAND_TEMP_STRING_CONSTANT;

                    result = new_tmp_stringslot ((unsigned char *) NULL);
                    t = tmp_stringslots[result];

                    switch (r1.result_type)
                    {
                        case OPERAND_INT_CONSTANT:
                            sprintf (tmp, "%d", r1.result);
                            copy_str2string (t, (unsigned char *) tmp);
                            break;
                        case OPERAND_STRING_CONSTANT:
                            copy_string2string (t, stringslots[r1.result]);
                            break;
                        case OPERAND_TEMP_STRING_CONSTANT:
                            copy_string2string (t, tmp_stringslots[r1.result]);
                            if (tmp_stringslots[r1.result]->flags & STRING_FLAG_TEMP_ACTIVE)
                            {
                                STRING * temp = t;
                                tmp_stringslots[r1.result]->flags &= ~STRING_FLAG_TEMP_ACTIVE;
                                tmp_stringslots[result] = tmp_stringslots[r1.result];
                                tmp_stringslots[r1.result] = temp;
                            }
                            else
                            {
                                fprintf (stderr, "internal error in evaluate_postfix(): temp string is not marked as temp string\n");
                            }
                            break;
                        case OPERAND_LOCAL_STRING_VARIABLE:
                            copy_string2string (t, stringslots[current_function->local_string_variables[r1.result]]);
                            break;
                        case OPERAND_LOCAL_STRING_ARRAY_VARIABLE:
                            evaluate_postfix_slot (r1.result_postfix_slot, &r_idx);
                            result_idx = get_result_int (&r_idx);

                            if (result_idx >= 0 && result_idx < current_function->local_string_arraysizes[r1.result])
                            {
                                copy_string2string (t, stringslots[current_function->local_string_array_variables[r1.result][result_idx]]);
                            }
                            else
                            {
                                fprintf (stderr, "fatal error: index %d of local string array[%d] is out of range (%d)\n",
                                                result_idx, current_function->local_string_arraysizes[r1.result], __LINE__);
                                exit (1);
                            }
                            break;
                        case OPERAND_GLOBAL_STRING_VARIABLE:
                            copy_string2string (t, stringslots[global_string_variables[r1.result]]);
                            break;
                        case OPERAND_GLOBAL_STRING_ARRAY_VARIABLE:
                            evaluate_postfix_slot (r1.result_postfix_slot, &r_idx);
                            result_idx = get_result_int (&r_idx);

                            if (result_idx >= 0 && result_idx < global_string_array_variables[r1.result].arraysize)
                            {
                                copy_string2string (t, stringslots[global_string_array_variables[r1.result].slots[result_idx]]);
                            }
                            else
                            {
                                fprintf (stderr, "fatal error: index %d of global string array[%d] is out of range (%d)\n",
                                                result_idx, global_string_array_variables[r1.result].arraysize, __LINE__);
                                exit (1);
                            }
                            break;
                        default:
                            fprintf (stderr, "internal error in evaluate_postfix(): unknown r1.result_type = %d (%d)\n", r1.result_type, __LINE__);
                            break;
                    }

                    switch (r2.result_type)
                    {
                        case OPERAND_INT_CONSTANT:
                            sprintf (tmp, "%d", r2.result);
                            concat_str2string (t, (unsigned char *) tmp);
                            break;

                        case OPERAND_STRING_CONSTANT:
                            concat_string2string (t, stringslots[r2.result]);
                            break;

                        case OPERAND_TEMP_STRING_CONSTANT:
                            concat_string2string (t, tmp_stringslots[r2.result]);

                            if (tmp_stringslots[r2.result]->flags & STRING_FLAG_TEMP_ACTIVE)
                            {
                                tmp_stringslots[r2.result]->flags &= ~STRING_FLAG_TEMP_ACTIVE;
                            }
                            else
                            {
                                fprintf (stderr, "internal error 1 in evaluate_postfix(): temp string is not marked as temp string");
                            }
                            break;

                        case OPERAND_LOCAL_STRING_VARIABLE:
                            concat_string2string (t, stringslots[current_function->local_string_variables[r2.result]]);
                            break;

                        case OPERAND_LOCAL_STRING_ARRAY_VARIABLE:
                            evaluate_postfix_slot (r2.result_postfix_slot, &r_idx);
                            result_idx = get_result_int (&r_idx);

                            if (result_idx >= 0 && result_idx < current_function->local_string_arraysizes[r2.result])
                            {
                                concat_string2string (t, stringslots[current_function->local_string_array_variables[r2.result][result_idx]]);
                            }
                            else
                            {
                                fprintf (stderr, "fatal error: index %d of local string array[%d] is out of range (%d)\n",
                                                result_idx, current_function->local_string_arraysizes[r2.result], __LINE__);
                                exit (1);
                            }
                            break;

                        case OPERAND_GLOBAL_STRING_VARIABLE:
                            concat_string2string (t, stringslots[global_string_variables[r2.result]]);
                            break;

                        case OPERAND_GLOBAL_STRING_ARRAY_VARIABLE:
                            evaluate_postfix_slot (r2.result_postfix_slot, &r_idx);
                            result_idx = get_result_int (&r_idx);

                            if (result_idx >= 0 && result_idx < global_string_array_variables[r2.result].arraysize)
                            {
                                concat_string2string (t, stringslots[global_string_array_variables[r2.result].slots[result_idx]]);
                            }
                            else
                            {
                                fprintf (stderr, "fatal error: index %d of global string array[%d] is out of range (%d)\n",
                                                result_idx, global_string_array_variables[r2.result].arraysize, __LINE__);
                                exit (1);
                            }
                            break;

                        default:
                            fprintf (stderr, "internal error in evaluate_postfix(): unknown r2.result_type = %d (%d)\n", r2.result_type, __LINE__);
                            break;
                    }
                }
                else
                {
                    int operand1;
                    int operand2;

                    result_type = OPERAND_INT_CONSTANT;
                    operand1 = get_result_int (&r1);
                    operand2 = get_result_int (&r2);

                    result = calc (p[idx].value, operand1, operand2);
                }

                push(stackp, result, result_type, -1);
                break;
            } // default
        } // switch
    }

    pop (stackp, rp);
    return OK;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 *  evaluate postfix slot
 *
 *  possible values of *result_typep:
 *
 *  OPERAND_INT_CONSTANT
 *  OPERAND_STRING_CONSTANT
 *  OPERAND_TEMP_STRING_CONSTANT
 *  OPERAND_LOCAL_STRING_VARIABLE
 *  OPERAND_LOCAL_STRING_ARRAY_VARIABLE
 *  OPERAND_GLOBAL_STRING_VARIABLE
 *  OPERAND_GLOBAL_STRING_ARRAY_VARIABLE
 *
 *  Here we only execute the optimization hints. All other expressions will be evaluated by evaluate_postfix().
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
evaluate_postfix_slot (int slot, RESULT * rp)
{
    POSTFIX_ELEMENT *   p       = postfix_slots[slot];
    int                 depth   = postfix_depth[slot];
    int                 hint    = postfix_hint[slot];
    int                 rtc     = OK;

    switch (hint)
    {
        case OPTIMIZER_HINT_CONST_NO_OP:                                // optimization: postfix contains only const or string or string-var, but no operator
        {
            rp->result_type = p[0].type;
            rp->result_postfix_slot = -1;
            rp->result = p[0].value;
            return OK;
        }
        case OPTIMIZER_HINT_LOC_INT_NO_OP:                              // optimization: postfix contains only local int variable, but no operator
        {
            rp->result_type = OPERAND_INT_CONSTANT;
            rp->result_postfix_slot = -1;
            rp->result = current_function->local_int_variables[p[0].value];
            return OK;
        }
        case OPTIMIZER_HINT_GLOB_INT_NO_OP:                             // optimization: postfix contains only global int variable, but no operator
        {
            rp->result_type = OPERAND_INT_CONSTANT;
            rp->result_postfix_slot = -1;
            rp->result = global_int_variables[p[0].value];
            return OK;
        }
        case OPTIMIZER_HINT_LOC_BYTE_NO_OP:                             // optimization: postfix contains only local int variable, but no operator
        {
            rp->result_type = OPERAND_INT_CONSTANT;
            rp->result_postfix_slot = -1;
            rp->result = current_function->local_byte_variables[p[0].value];
            return OK;
        }
        case OPTIMIZER_HINT_GLOB_BYTE_NO_OP:                            // optimization: postfix contains only global int variable, but no operator
        {
            rp->result_type = OPERAND_INT_CONSTANT;
            rp->result_postfix_slot = -1;
            rp->result = global_byte_variables[p[0].value];
            return OK;
        }
        case OPTIMIZER_HINT_LOC_INT_LOC_INT_OP:                         // optimization: local variable OPERATOR local variable
        {
            int val1;
            int val2;

            val1 = current_function->local_int_variables[p[0].value];
            val2 = current_function->local_int_variables[p[1].value];
            rp->result_postfix_slot = -1;
            rp->result_type = OPERAND_INT_CONSTANT;
            rp->result = calc (p[2].value, val1, val2);
            return OK;
        }
        case OPTIMIZER_HINT_LOC_INT_CONST_INT_OP:                       // optimization: local variable OPERATOR constant
        {
            int val1;
            int val2;

            val1 = current_function->local_int_variables[p[0].value];
            val2 = p[1].value;
            rp->result_postfix_slot = -1;
            rp->result_type = OPERAND_INT_CONSTANT;
            rp->result = calc (p[2].value, val1, val2);
            return OK;
        }
        case OPTIMIZER_HINT_GLOB_INT_GLOB_INT_OP:                       // optimization: global variable OPERATOR global variable
        {
            int val1;
            int val2;

            val1 = global_int_variables[p[0].value];
            val2 = global_int_variables[p[1].value];
            rp->result_postfix_slot = -1;
            rp->result_type = OPERAND_INT_CONSTANT;
            rp->result = calc (p[2].value, val1, val2);
            return OK;
        }
        case OPTIMIZER_HINT_GLOB_INT_CONST_INT_OP:                      // optimization: global variable OPERATOR constant
        {
            int val1;
            int val2;

            val1 = global_int_variables[p[0].value];
            val2 = p[1].value;
            rp->result_postfix_slot = -1;
            rp->result_type = OPERAND_INT_CONSTANT;
            rp->result = calc (p[2].value, val1, val2);
            return OK;
        }
        case OPTIMIZER_HINT_INT_FUNC_NO_OP:                             // optimization: postfix contains only call of intern function, but no operator
        {
            int         i = p[0].value;
            FIP_RUN *   fip = fip_run_slots[i];
            int         func_idx = fip->func_idx;
            int         return_type;

            rp->result_postfix_slot = -1;
            return_type = (*func[func_idx])(fip);

            switch (return_type)
            {
                case FUNCTION_TYPE_INT:
                    rp->result_type = OPERAND_INT_CONSTANT;
                    rp->result = fip->reti;
                    return OK;
                case FUNCTION_TYPE_STRING:
                    rp->result_type = OPERAND_TEMP_STRING_CONSTANT;
                    rp->result = fip->reti;
                    return OK;
                default: // FUNCTION_TYPE_VOID
                    rp->result_type = OPERAND_INT_CONSTANT;               // function is void, return dummy
                    rp->result = 0;
                    return OK;
            }

            // TODO: Saemtliche TEMP_STRINGS in Argumenten freigeben, wenn die Funktion _nicht_ get_argumentxxx() aufruft.
            // kill_temp_strings (POSTFIX_ELEMENT * p)
            break;
        }
        case OPTIMIZER_HINT_EXT_FUNC_NO_OP:                             // optimization: postfix contains only call of extern function, but no operator
        {
            FUNCTION *  save_current_function;
            FIP_RUN *   fip         = fip_run_slots[p[0].value];
            int         func_idx    = fip->func_idx;
            FUNCTION *  fp          = functions + func_idx;

            if (fp->argc != fip->argc)
            {
                fprintf (stderr, "internal runtime error: func_idx = %d, fp->argc = %d, fip->argc = %d\n", func_idx, fp->argc, fip->argc);
                return -1;
            }

            save_current_function = current_function;

            if (nici (func_idx, fip) < 0)
            {
                return -1;
            }

            current_function = save_current_function;

            rp->result_postfix_slot = -1;

            switch (functions[func_idx].return_type)
            {
                case FUNCTION_TYPE_INT:
                    rp->result_type = OPERAND_INT_CONSTANT;
                    rp->result = fip->reti;
                    return OK;
                case FUNCTION_TYPE_STRING:
                    rp->result_type = OPERAND_TEMP_STRING_CONSTANT;
                    rp->result = fip->reti;
                    return OK;
                default: // FUNCTION_TYPE_VOID
                    rp->result_type = OPERAND_INT_CONSTANT;               // function is void, return dummy
                    rp->result = 0;
                    return OK;
            }
            break;
        }
        case OPTIMIZER_HINT_NONE:
        {
            break;
        }
        default:
        {
            fprintf (stderr, "internal error in evaluate_postfix_slot(): unknown optimizer hint: %d (%d)\n", hint, __LINE__);
            break;
        }
    }

#if 0
    printf ("hint = %d ", hint);
    print_postfix_slot (slot);
#endif

    rtc = evaluate_postfix (p, depth, rp);

    return rtc;
}

static int
check_condition (int st_idx)
{
    int             current_postfix_slot1;
    int             current_postfix_slot2;
    RESULT          r1;
    RESULT          r2;
    int             result1;
    int             result2;
    unsigned char * s1 = (unsigned char *) NULL;
    unsigned char * s2 = (unsigned char *) NULL;
    int             operator;

    current_postfix_slot1 = statementp[st_idx].st.st_if.postfix_slot1;
    if (evaluate_postfix_slot(current_postfix_slot1, &r1) < 0)
    {
        return -1;
    }

    current_postfix_slot2 = statementp[st_idx].st.st_if.postfix_slot2;
    if (evaluate_postfix_slot(current_postfix_slot2, &r2) < 0)
    {
        return -1;
    }

    if (r1.result_type == OPERAND_INT_CONSTANT || r2.result_type == OPERAND_INT_CONSTANT)               // if one of the expressions are of type int,
    {                                                                                                   // then we compare all expressions as int values
        result1 = get_result_int (&r1);
        result2 = get_result_int (&r2);

        operator = statementp[st_idx].st.st_if.operator;

        if ((operator == EQUAL_COMPARE_OPERATOR         && result1 == result2) ||
            (operator == NOT_EQUAL_COMPARE_OPERATOR     && result1 != result2) ||
            (operator == LESS_COMPARE_OPERATOR          && result1 <  result2) ||
            (operator == LESS_EQUAL_COMPARE_OPERATOR    && result1 <= result2) ||
            (operator == GREATER_COMPARE_OPERATOR       && result1 >  result2) ||
            (operator == GREATER_EQUAL_COMPARE_OPERATOR && result1 >= result2))
        {
            return 1;
        }
    }
    else
    {                                                                                       // both expressions are of type string, let's compare them as strings
        RESULT  r_idx;
        int     result_idx;

        switch (r1.result_type)
        {
            default:
            case OPERAND_STRING_CONSTANT:
                s1 = stringslots[r1.result]->str;
                break;
            case OPERAND_TEMP_STRING_CONSTANT:
                if (tmp_stringslots[r1.result]->flags & STRING_FLAG_TEMP_ACTIVE)
                {
                    tmp_stringslots[r1.result]->flags &= ~STRING_FLAG_TEMP_ACTIVE;
                }
                else
                {
                    fprintf (stderr, "internal error in nici(): temp string [%d] '%s' is not marked as temp string (%d)\n",
                                r1.result, tmp_stringslots[r1.result]->str, __LINE__);
                }
                s1 = tmp_stringslots[r1.result]->str;
                break;
            case OPERAND_LOCAL_STRING_VARIABLE:
                s1 = stringslots[current_function->local_string_variables[r1.result]]->str;
                break;
            case OPERAND_LOCAL_STRING_ARRAY_VARIABLE:
                if (evaluate_postfix_slot (r1.result_postfix_slot, &r_idx) < 0)
                {
                    return -1;
                }

                result_idx = get_result_int (&r_idx);

                if (result_idx >= 0 && result_idx < current_function->local_string_arraysizes[r1.result])
                {
                    s1 = stringslots[current_function->local_string_array_variables[r1.result][result_idx]]->str;
                }
                else
                {
                    fprintf (stderr, "fatal error line %d: index %d of local string array[%d] is out of range (%d)\n",
                                    statementp[st_idx].line, result_idx, current_function->local_string_arraysizes[r1.result], __LINE__);
                    exit (1);
                }
                break;
            case OPERAND_GLOBAL_STRING_VARIABLE:
                s1 = stringslots[global_string_variables[r1.result]]->str;
                break;
            case OPERAND_GLOBAL_STRING_ARRAY_VARIABLE:
                if (evaluate_postfix_slot (r1.result_postfix_slot, &r_idx) < 0)
                {
                    return -1;
                }

                result_idx = get_result_int (&r_idx);

                if (result_idx >= 0 && result_idx < global_string_array_variables[r1.result].arraysize)
                {
                    s1 = stringslots[global_string_array_variables[r1.result].slots[result_idx]]->str;
                }
                else
                {
                    fprintf (stderr, "fatal error line %d: index %d of global string array[%d] is out of range (%d)\n",
                                    statementp[st_idx].line, result_idx, global_string_array_variables[r1.result].arraysize, __LINE__);
                    exit (1);
                }
                break;
        }

        switch (r2.result_type)
        {
            default:
            case OPERAND_STRING_CONSTANT:
                s2 = stringslots[r2.result]->str;
                break;
            case OPERAND_TEMP_STRING_CONSTANT:
                if (tmp_stringslots[r2.result]->flags & STRING_FLAG_TEMP_ACTIVE)
                {
                    tmp_stringslots[r2.result]->flags &= ~STRING_FLAG_TEMP_ACTIVE;
                }
                else
                {
                    fprintf (stderr, "internal error in nici(): temp string [%d] '%s' is not marked as temp string (%d)\n",
                                r2.result, tmp_stringslots[r2.result]->str, __LINE__);
                }
                s2 = tmp_stringslots[r2.result]->str;
                break;
            case OPERAND_LOCAL_STRING_VARIABLE:
                s2 = stringslots[current_function->local_string_variables[r2.result]]->str;
                break;
            case OPERAND_LOCAL_STRING_ARRAY_VARIABLE:
                if (evaluate_postfix_slot (r2.result_postfix_slot, &r_idx) < 0)
                {
                    return -1;
                }
                result_idx = get_result_int (&r_idx);

                if (result_idx >= 0 && result_idx < current_function->local_string_arraysizes[r2.result])
                {
                    s2 = stringslots[current_function->local_string_array_variables[r2.result][result_idx]]->str;
                }
                else
                {
                    fprintf (stderr, "fatal error line %d: index %d of local string array[%d] is out of range (%d)\n",
                                    statementp[st_idx].line, result_idx, current_function->local_string_arraysizes[r2.result], __LINE__);
                    exit (1);
                }
                break;
            case OPERAND_GLOBAL_STRING_VARIABLE:
                s2 = stringslots[global_string_variables[r2.result]]->str;
                break;
            case OPERAND_GLOBAL_STRING_ARRAY_VARIABLE:
                if (evaluate_postfix_slot (r2.result_postfix_slot, &r_idx) < 0)
                {
                    return -1;
                }

                result_idx = get_result_int (&r_idx);

                if (result_idx >= 0 && result_idx < global_string_array_variables[r2.result].arraysize)
                {
                    s2 = stringslots[global_string_array_variables[r2.result].slots[result_idx]]->str;
                }
                else
                {
                    fprintf (stderr, "fatal error line %d: index %d of global string array[%d] is out of range (%d)\n",
                                    statementp[st_idx].line, result_idx, global_string_array_variables[r2.result].arraysize, __LINE__);
                    exit (1);
                }
                break;
        }

        operator = statementp[st_idx].st.st_if.operator;

        if ((operator == EQUAL_COMPARE_OPERATOR         && ustrcmp (s1, s2) == 0) ||
            (operator == NOT_EQUAL_COMPARE_OPERATOR     && ustrcmp (s1, s2) != 0) ||
            (operator == LESS_COMPARE_OPERATOR          && ustrcmp (s1, s2) <  0) ||
            (operator == LESS_EQUAL_COMPARE_OPERATOR    && ustrcmp (s1, s2) <= 0) ||
            (operator == GREATER_COMPARE_OPERATOR       && ustrcmp (s1, s2) >  0) ||
            (operator == GREATER_EQUAL_COMPARE_OPERATOR && ustrcmp (s1, s2) >= 0))
        {
            return 1;
        }
    }

    return 0;
}

int
nici (int func_idx, FIP_RUN * fip)
{
    int         st_idx;
    int         current_postfix_slot;
    int **      save_local_int_array_variables;
    uint8_t **  save_local_byte_array_variables;
    int **      save_local_string_array_variables;
    int         result_idx;
    int         i;
    int         j;

    FUNCTION *  new_function = functions + func_idx;

    if (new_function->local_int_variables_used)
    {
        if (! local_int_variable_stack_used + new_function->local_int_variables_used >= local_int_variable_stack_allocated)
        {
            local_int_variable_stack_allocated += new_function->local_int_variables_used + LOCAL_VARIABLE_STACK_ALLOC_GRANULARITY;
            local_int_variable_stack = alloc_realloc (__FILE__, __LINE__, local_int_variable_stack, local_int_variable_stack_allocated * sizeof (int));
        }

        new_function->local_int_variables = local_int_variable_stack + local_int_variable_stack_used;
        local_int_variable_stack_used += new_function->local_int_variables_used;

        for (i = 0; i < new_function->local_int_variables_used; i++)
        {
            new_function->local_int_variables[i] = 0;
        }
    }
    else
    {
        new_function->local_int_variables = (int *) NULL;
    }

    if (new_function->local_byte_variables_used)
    {
        if (! local_byte_variable_stack_used + new_function->local_byte_variables_used >= local_byte_variable_stack_allocated)
        {
            local_byte_variable_stack_allocated += new_function->local_byte_variables_used + LOCAL_VARIABLE_STACK_ALLOC_GRANULARITY;
            local_byte_variable_stack = alloc_realloc (__FILE__, __LINE__, local_byte_variable_stack, local_byte_variable_stack_allocated * sizeof (uint8_t));
        }

        new_function->local_byte_variables = local_byte_variable_stack + local_byte_variable_stack_used;
        local_byte_variable_stack_used += new_function->local_byte_variables_used;

        for (i = 0; i < new_function->local_byte_variables_used; i++)
        {
            new_function->local_byte_variables[i] = 0;
        }
    }
    else
    {
        new_function->local_byte_variables = (uint8_t *) NULL;
    }

    if (new_function->local_string_variables_used)
    {
        if (! local_string_variable_stack_used + new_function->local_string_variables_used >= local_string_variable_stack_allocated)
        {
            local_string_variable_stack_allocated += new_function->local_string_variables_used + LOCAL_VARIABLE_STACK_ALLOC_GRANULARITY;
            local_string_variable_stack = alloc_realloc (__FILE__, __LINE__, local_string_variable_stack, local_string_variable_stack_allocated * sizeof (int));
        }

        new_function->local_string_variables = local_string_variable_stack + local_string_variable_stack_used;
        local_string_variable_stack_used += new_function->local_string_variables_used;

        for (i = 0; i < new_function->local_string_variables_used; i++)
        {
            new_function->local_string_variables[i] = new_stringslot ((unsigned char *) "");
        }
    }
    else
    {
        new_function->local_string_variables = (int *) NULL;
    }

    save_local_int_array_variables      = new_function->local_int_array_variables;
    save_local_byte_array_variables     = new_function->local_byte_array_variables;
    save_local_string_array_variables   = new_function->local_string_array_variables;

    if (new_function->local_int_array_variables_used)
    {
        new_function->local_int_array_variables = alloc_malloc (__FILE__, __LINE__, new_function->local_int_array_variables_used * sizeof (int **));

        for (i = 0; i < new_function->local_int_array_variables_used; i++)
        {
            new_function->local_int_array_variables[i] = alloc_calloc (__FILE__, __LINE__, new_function->local_int_arraysizes[i], sizeof (int));
        }
    }
    else
    {
        new_function->local_int_array_variables = (int **) NULL;
    }


    if (new_function->local_byte_array_variables_used)
    {
        new_function->local_byte_array_variables = alloc_malloc (__FILE__, __LINE__, new_function->local_byte_array_variables_used * sizeof (uint8_t **));

        for (i = 0; i < new_function->local_byte_array_variables_used; i++)
        {
            new_function->local_byte_array_variables[i] = alloc_calloc (__FILE__, __LINE__, new_function->local_byte_arraysizes[i], sizeof (uint8_t));
        }
    }
    else
    {
        new_function->local_byte_array_variables = (uint8_t **) NULL;
    }

    if (new_function->local_string_array_variables_used)
    {
        new_function->local_string_array_variables = alloc_malloc (__FILE__, __LINE__, new_function->local_string_array_variables_used * sizeof (int **));

        for (i = 0; i < new_function->local_string_array_variables_used; i++)
        {
            new_function->local_string_array_variables[i] = alloc_calloc (__FILE__, __LINE__, new_function->local_string_arraysizes[i], sizeof (int));

            for (j = 0; j < new_function->local_string_arraysizes[i]; j++)
            {
                new_function->local_string_array_variables[i][j] = new_stringslot ((unsigned char *) "");
            }
        }
    }
    else
    {
        new_function->local_string_array_variables = (int **) NULL;
    }

    if (fip)
    {
        for (i = 0; i < new_function->argc; i++)
        {
            if (new_function->argtypes[i] == ARGUMENT_TYPE_INT)
            {
                new_function->local_int_variables[new_function->argvars[i]] = get_argument_int (fip, i);            // get_argument_xxx() uses current_function!
            }
            else if (new_function->argtypes[i] == ARGUMENT_TYPE_BYTE)
            {
                new_function->local_byte_variables[new_function->argvars[i]] = get_argument_byte (fip, i);          // get_argument_xxx() uses current_function!
            }
            else if (new_function->argtypes[i] == ARGUMENT_TYPE_STRING)
            {
                copy_str2string (stringslots[new_function->local_string_variables[new_function->argvars[i]]], get_argument_string (fip, i));
            }
            else
            {
                // TODO
            }
        }
    }
    else                                                                                                            // it's the main function
    {
        for (i = 0; i < main_argc; i++)
        {
            if (new_function->argtypes[i] == ARGUMENT_TYPE_INT)
            {
                new_function->local_int_variables[new_function->argvars[i]] = atoi (main_argv[i]);
            }
            else if (new_function->argtypes[i] == ARGUMENT_TYPE_BYTE)
            {
                // not implemented
            }
            else if (new_function->argtypes[i] == ARGUMENT_TYPE_STRING)
            {
                copy_str2string (stringslots[new_function->local_string_variables[new_function->argvars[i]]], (unsigned char *) main_argv[i]);
            }
            else
            {
                // TODO
            }
        }
    }

    current_function = new_function;

    st_idx = current_function->first_statement_idx;

    while (st_idx < statements_used)
    {
        if (alarm_slots_used)
        {
            update_alarm_timers ();
        }
        if (console_interrupted())
        {
            return -1;
        }

        switch (statementp[st_idx].type)
        {
            case STATEMENT_TYPE_INCREMENT:
            {
                int     variable_idx;
                int     variable_type;
                int     step;

                variable_idx    = statementp[st_idx].st.st_increment.variable_idx;
                variable_type   = statementp[st_idx].st.st_increment.variable_type;
                step            = statementp[st_idx].st.st_increment.step;

                if (variable_type == VARIABLE_TYPE_LOCAL_INT)
                {
                    current_function->local_int_variables[variable_idx] += step;
                }
                else if (variable_type == VARIABLE_TYPE_GLOBAL_INT)
                {
                    global_int_variables[variable_idx] += step;
                }
                else if (variable_type == VARIABLE_TYPE_LOCAL_BYTE)
                {
                    current_function->local_byte_variables[variable_idx] += step;
                }
                else if (variable_type == VARIABLE_TYPE_GLOBAL_BYTE)
                {
                    global_byte_variables[variable_idx] += step;
                }
                else
                {
                    fprintf (stderr, "internal error in nici(): unknown variable_type = %d (%d)\n", variable_type, __LINE__);
                }
                st_idx = statementp[st_idx].next;
                break;
            }
            case STATEMENT_TYPE_INTERN_FUNCTION:
            {
                int     assignment_variable_idx;
                int     assignment_variable_type;
                int     assignment_variable_pslot;
                RESULT  r;
                RESULT  r_idx;

                current_postfix_slot = statementp[st_idx].st.st_intern_function.postfix_slot;

                // print_postfix_slot (current_postfix_slot);
                if (evaluate_postfix_slot(current_postfix_slot, &r) < 0)
                {
                    return -1;
                }

                assignment_variable_idx     = statementp[st_idx].st.st_intern_function.assignment_variable_idx;
                assignment_variable_type    = statementp[st_idx].st.st_intern_function.assignment_variable_type;
                assignment_variable_pslot   = statementp[st_idx].st.st_intern_function.assignment_variable_pslot;

                if (assignment_variable_idx >= 0)
                {
                    if (assignment_variable_type == VARIABLE_TYPE_LOCAL_INT || assignment_variable_type == VARIABLE_TYPE_GLOBAL_INT)
                    {
                        int result = get_result_int (&r);

                        if (assignment_variable_type == VARIABLE_TYPE_LOCAL_INT)
                        {
                            current_function->local_int_variables[assignment_variable_idx] = result;
                        }
                        else
                        {
                            global_int_variables[assignment_variable_idx] = result;
                        }
                    }
                    else if (assignment_variable_type == VARIABLE_TYPE_LOCAL_INT_ARRAY || assignment_variable_type == VARIABLE_TYPE_GLOBAL_INT_ARRAY)
                    {
                        int result = get_result_int (&r);

                        if (evaluate_postfix_slot (assignment_variable_pslot, &r_idx) < 0)
                        {
                            return -1;
                        }
                        result_idx = get_result_int (&r_idx);

                        if (assignment_variable_type == VARIABLE_TYPE_LOCAL_INT_ARRAY)
                        {
                            if (result_idx >= 0 && result_idx < current_function->local_int_arraysizes[assignment_variable_idx])
                            {
                                current_function->local_int_array_variables[assignment_variable_idx][result_idx] = result;
                            }
                            else
                            {
                                fprintf (stderr, "fatal error line %d: index %d of local int array[%d] is out of range (%d)\n",
                                                statementp[st_idx].line, result_idx, current_function->local_int_arraysizes[assignment_variable_idx], __LINE__);
                                exit (1);
                            }
                        }
                        else
                        {
                            if (result_idx >= 0 && result_idx < global_int_array_variables[assignment_variable_idx].arraysize)
                            {
                                global_int_array_variables[assignment_variable_idx].values[result_idx] = result;
                            }
                            else
                            {
                                fprintf (stderr, "fatal error line %d: index %d of global int array[%d] is out of range (%d)\n",
                                                statementp[st_idx].line, result_idx, global_int_array_variables[assignment_variable_idx].arraysize, __LINE__);
                                exit (1);
                            }
                        }
                    }
                    else if (assignment_variable_type == VARIABLE_TYPE_LOCAL_BYTE || assignment_variable_type == VARIABLE_TYPE_GLOBAL_BYTE)
                    {
                        int result = get_result_int (&r);

                        if (assignment_variable_type == VARIABLE_TYPE_LOCAL_BYTE)
                        {
                            current_function->local_byte_variables[assignment_variable_idx] = result;
                        }
                        else
                        {
                            global_byte_variables[assignment_variable_idx] = result;
                        }
                    }
                    else if (assignment_variable_type == VARIABLE_TYPE_LOCAL_BYTE_ARRAY || assignment_variable_type == VARIABLE_TYPE_GLOBAL_BYTE_ARRAY)
                    {
                        int result = get_result_int (&r);

                        if (evaluate_postfix_slot (assignment_variable_pslot, &r_idx) < 0)
                        {
                            return -1;
                        }
                        result_idx = get_result_int (&r_idx);

                        if (assignment_variable_type == VARIABLE_TYPE_LOCAL_BYTE_ARRAY)
                        {
                            if (result_idx >= 0 && result_idx < current_function->local_byte_arraysizes[assignment_variable_idx])
                            {
                                current_function->local_byte_array_variables[assignment_variable_idx][result_idx] = result;
                            }
                            else
                            {
                                fprintf (stderr, "fatal error line %d: index %d of local byte array[%d] is out of range (%d)\n",
                                                statementp[st_idx].line, result_idx, current_function->local_byte_arraysizes[assignment_variable_idx], __LINE__);
                                exit (1);
                            }
                        }
                        else
                        {
                            if (result_idx >= 0 && result_idx < global_byte_array_variables[assignment_variable_idx].arraysize)
                            {
                                global_byte_array_variables[assignment_variable_idx].values[result_idx] = result;
                            }
                            else
                            {
                                fprintf (stderr, "fatal error line %d: index %d of global byte array[%d] is out of range (%d)\n",
                                                statementp[st_idx].line, result_idx, global_byte_array_variables[assignment_variable_idx].arraysize, __LINE__);
                                exit (1);
                            }
                        }
                    }
                    else
                    {
                        unsigned char tmp[32];
                        STRING * t = (STRING *) NULL;
                        STRING ** x = (STRING **) NULL;

                        if (assignment_variable_type == VARIABLE_TYPE_LOCAL_STRING)
                        {
                            x = &(stringslots[current_function->local_string_variables[assignment_variable_idx]]);
                        }
                        else if (assignment_variable_type == VARIABLE_TYPE_LOCAL_STRING_ARRAY)
                        {
                            if (evaluate_postfix_slot (assignment_variable_pslot, &r_idx) < 0)
                            {
                                return -1;
                            }
                            result_idx = get_result_int (&r_idx);

                            if (result_idx >= 0 && result_idx < current_function->local_string_arraysizes[assignment_variable_idx])
                            {
                                x = &(stringslots[current_function->local_string_array_variables[assignment_variable_idx][result_idx]]);
                            }
                            else
                            {
                                fprintf (stderr, "fatal error line %d: index %d of local string array[%d] is out of range (%d)\n",
                                                statementp[st_idx].line, result_idx, current_function->local_string_arraysizes[assignment_variable_idx], __LINE__);
                                exit (1);
                            }
                        }
                        else if (assignment_variable_type == VARIABLE_TYPE_GLOBAL_STRING)
                        {
                            x = &(stringslots[global_string_variables[assignment_variable_idx]]);
                        }
                        else if (assignment_variable_type == VARIABLE_TYPE_GLOBAL_STRING_ARRAY)
                        {
                            if (evaluate_postfix_slot (assignment_variable_pslot, &r_idx) < 0)
                            {
                                return -1;
                            }
                            result_idx = get_result_int (&r_idx);

                            if (result_idx >= 0 && result_idx < global_string_array_variables[assignment_variable_idx].arraysize)
                            {
                                x = &(stringslots[global_string_array_variables[assignment_variable_idx].slots[result_idx]]);
                            }
                            else
                            {
                                fprintf (stderr, "fatal error line %d: index %d of global string array[%d] is out of range (%d)\n",
                                                statementp[st_idx].line, result_idx, global_string_array_variables[assignment_variable_idx].arraysize, __LINE__);
                                exit (1);
                            }
                        }
                        else
                        {
                            fprintf (stderr, "internal error in nici(): unknown assignment_variable_type = %d (%d)\n", assignment_variable_type, __LINE__);
                        }

                        t = *x;

                        switch (r.result_type)
                        {
                            case OPERAND_INT_CONSTANT:
                                sprintf ((char *) tmp, "%d", r.result);
                                copy_str2string (t, tmp);
                                break;
                            case OPERAND_STRING_CONSTANT:
                                copy_string2string (t, stringslots[r.result]);
                                break;
                            case OPERAND_TEMP_STRING_CONSTANT:
                            {
                                if (tmp_stringslots[r.result]->flags & STRING_FLAG_TEMP_ACTIVE)
                                {
                                    tmp_stringslots[r.result]->flags &= ~STRING_FLAG_TEMP_ACTIVE;
                                }
                                else
                                {
                                    fprintf (stderr, "internal error in nici(): temp string [%d] '%s' is not marked as temp string (%d)\n",
                                                r.result, tmp_stringslots[r.result]->str, __LINE__);
                                }

                                *x = tmp_stringslots[r.result];
                                tmp_stringslots[r.result] = t;
                                break;
                            }
                            case OPERAND_LOCAL_STRING_VARIABLE:
                                copy_string2string (t, stringslots[current_function->local_string_variables[assignment_variable_idx]]);
                                break;
                            case OPERAND_LOCAL_STRING_ARRAY_VARIABLE:
                                if (evaluate_postfix_slot (r.result_postfix_slot, &r_idx) < 0)
                                {
                                    return -1;
                                }
                                result_idx = get_result_int (&r_idx);

                                if (result_idx >= 0 && result_idx < current_function->local_string_arraysizes[assignment_variable_idx])
                                {
                                    copy_string2string (t, stringslots[current_function->local_string_array_variables[assignment_variable_idx][result_idx]]);
                                }
                                else
                                {
                                    fprintf (stderr, "fatal error line %d: index %d of local string array[%d] is out of range (%d)\n",
                                                    statementp[st_idx].line, result_idx, current_function->local_string_arraysizes[assignment_variable_idx], __LINE__);
                                    exit (1);
                                }
                                break;
                            case OPERAND_GLOBAL_STRING_VARIABLE:
                                copy_string2string (t, stringslots[global_string_variables[assignment_variable_idx]]);
                                break;
                            case OPERAND_GLOBAL_STRING_ARRAY_VARIABLE:
                                if (evaluate_postfix_slot (r.result_postfix_slot, &r_idx) < 0)
                                {
                                    return -1;
                                }
                                result_idx = get_result_int (&r_idx);

                                if (result_idx >= 0 && result_idx < global_string_array_variables[assignment_variable_idx].arraysize)
                                {
                                    copy_string2string (t, stringslots[global_string_array_variables[assignment_variable_idx].slots[result_idx]]);
                                }
                                else
                                {
                                    fprintf (stderr, "fatal error line %d: index %d of global string array[%d] is out of range (%d)\n",
                                                    statementp[st_idx].line, result_idx, global_string_array_variables[assignment_variable_idx].arraysize, __LINE__);
                                    exit (1);
                                }
                                break;
                            default:
                                fprintf (stderr, "internal error in nici(): unknown result_type = %d (%d)\n", r.result_type, __LINE__);
                                break;
                        }
                    }
                }
                else                                                                                // function returns value, but ignoring
                {
                    if (r.result_type == OPERAND_TEMP_STRING_CONSTANT)
                    {
                        if (tmp_stringslots[r.result]->flags & STRING_FLAG_TEMP_ACTIVE)
                        {
                            tmp_stringslots[r.result]->flags &= ~STRING_FLAG_TEMP_ACTIVE;
                        }
                        else
                        {
                            fprintf (stderr, "internal error in nici(): temp string [%d] '%s' is not marked as temp string (%d)\n",
                                        r.result, tmp_stringslots[r.result]->str, __LINE__);
                        }
                    }
                }
                st_idx = statementp[st_idx].next;
                break;
            }

            case STATEMENT_TYPE_IF:
            {
                int check_rtc = check_condition (st_idx);

                if (check_rtc > 0)
                {
                    st_idx = statementp[st_idx].next;
                }
                else if (check_rtc == 0)
                {
                    st_idx = statementp[st_idx].st.st_if.false_idx;
                }
                else
                {
                    return -1;
                }
                break;
            }

            case STATEMENT_TYPE_ENDIF:
            {
                st_idx = statementp[st_idx].next;
                break;
            }

            case STATEMENT_TYPE_WHILE:
            {
                int check_rtc;
                int endwhile_idx = statementp[st_idx].st.st_while.endwhile_idx;

                check_rtc = check_condition (st_idx);

                if (check_rtc > 0)
                {
                    st_idx = statementp[st_idx].next;
                }
                else if  (check_rtc == 0)
                {
                    st_idx = statementp[endwhile_idx].next;
                }
                else
                {
                    return -1;
                }
                break;
            }

            case STATEMENT_TYPE_ENDWHILE:
            {
                st_idx = statementp[st_idx].st.st_endwhile.while_idx;
                break;
            }

            case STATEMENT_TYPE_FOR:
            {
                RESULT  r_start;
                RESULT  r_stop;
                RESULT  r_step;
                int     result_start;
                int     result_stop;
                int     result_step;
                int     varidx;
                int     vartype;
                int     endfor_idx;

                endfor_idx  = statementp[st_idx].st.st_for.endfor_idx;
                varidx      = statementp[st_idx].st.st_for.for_variable_idx;
                vartype     = statementp[st_idx].st.st_for.for_variable_type;

                if (varidx >= 0 && (vartype == VARIABLE_TYPE_LOCAL_INT || vartype == VARIABLE_TYPE_GLOBAL_INT))
                {
                    current_postfix_slot = statementp[st_idx].st.st_for.postfix_slot_start;
                    if (evaluate_postfix_slot(current_postfix_slot, &r_start) < 0)
                    {
                        return -1;
                    }
                    result_start = get_result_int (&r_start);

                    if (vartype == VARIABLE_TYPE_LOCAL_INT)
                    {
                        current_function->local_int_variables[varidx] = result_start;
                    }
                    else
                    {
                        global_int_variables[varidx] = result_start;
                    }

                    current_postfix_slot = statementp[st_idx].st.st_for.postfix_slot_stop;
                    if (evaluate_postfix_slot(current_postfix_slot, &r_stop) < 0)
                    {
                        return -1;
                    }
                    result_stop = get_result_int (&r_stop);

                    current_postfix_slot = statementp[st_idx].st.st_for.postfix_slot_step;

                    if (current_postfix_slot >= 0)
                    {
                        if (evaluate_postfix_slot(current_postfix_slot, &r_step) < 0)
                        {
                            return -1;
                        }
                        result_step = get_result_int (&r_step);
                    }
                    else
                    {
                        result_step = 1;
                    }

                    if ((result_step >= 0 && result_start <= result_stop) ||
                        (result_step <  0 && result_start >= result_stop))
                    {
                        statementp[endfor_idx].st.st_endfor.stop_value = result_stop;
                        statementp[endfor_idx].st.st_endfor.step_value = result_step;
                        st_idx = statementp[st_idx].next;
                    }
                    else
                    {
                        st_idx = statementp[endfor_idx].next;
                    }
                }
                else
                {
                    fprintf (stderr, "internal error in nici(): for variable is no integer (%d)\n", __LINE__);
                }

                break;
            }

            case STATEMENT_TYPE_ENDFOR:
            {
                int     varidx;
                int     vartype;
                int     result_stop;
                int     step;
                int     for_idx;
                int     result;

                for_idx     = statementp[st_idx].st.st_endfor.for_idx;
                result_stop = statementp[st_idx].st.st_endfor.stop_value;
                step        = statementp[st_idx].st.st_endfor.step_value;

                varidx      = statementp[for_idx].st.st_for.for_variable_idx;
                vartype     = statementp[for_idx].st.st_for.for_variable_type;

                if (vartype == VARIABLE_TYPE_LOCAL_INT)
                {
                    current_function->local_int_variables[varidx] += step;
                    result = current_function->local_int_variables[varidx];
                }
                else
                {
                    global_int_variables[varidx] += step;
                    result = global_int_variables[varidx];
                }

                if ((step >= 0 && result <= result_stop) ||
                    (step <  0 && result >= result_stop))
                {
                    st_idx = statementp[for_idx].next;
                }
                else
                {
                    st_idx = statementp[st_idx].next;
                }

                break;
            }

            case STATEMENT_TYPE_REPEAT:
            {
                RESULT  r;

                int endrepeat_idx = statementp[st_idx].st.st_repeat.endrepeat_idx;

                current_postfix_slot = statementp[st_idx].st.st_repeat.postfix_slot;
                if (evaluate_postfix_slot(current_postfix_slot, &r) < 0)
                {
                    return -1;
                }

                statementp[endrepeat_idx].st.st_endrepeat.value = r.result;

                if (r.result > 0)
                {
                    st_idx = statementp[st_idx].next;
                }
                else
                {
                    st_idx = statementp[statementp[st_idx].st.st_repeat.endrepeat_idx].next;
                }
                break;
            }

            case STATEMENT_TYPE_ENDREPEAT:
            {
                if (statementp[st_idx].st.st_endrepeat.value > 0)
                {
                    statementp[st_idx].st.st_endrepeat.value--;

                    if (statementp[st_idx].st.st_endrepeat.value > 0)
                    {
                        st_idx = statementp[statementp[st_idx].st.st_endrepeat.repeat_idx].next;
                    }
                    else
                    {
                        st_idx = statementp[st_idx].next;
                    }
                }
                else
                {
                    st_idx = statementp[st_idx].next;
                }

                break;
            }

            case STATEMENT_TYPE_LOOP:
            {
                st_idx = statementp[st_idx].next;
                break;
            }

            case STATEMENT_TYPE_ENDLOOP:
            {
                st_idx = statementp[statementp[st_idx].st.st_endloop.loop_idx].next;
                break;
            }

            case STATEMENT_TYPE_BREAK:
            {
                st_idx = statementp[st_idx].next;
                break;
            }

            case STATEMENT_TYPE_CONTINUE:
            {
                st_idx = statementp[st_idx].next;
                break;
            }

            case STATEMENT_TYPE_RETURN:
            {
                RESULT r;

                if (statementp[st_idx].st.st_return.postfix_slot >= 0)                  // returning value or void function returning nothing?
                {
                    int     result;

                    if (fip)
                    {

                        current_postfix_slot = statementp[st_idx].st.st_return.postfix_slot;
                        if (evaluate_postfix_slot (current_postfix_slot, &r) < 0)
                        {
                            return -1;
                        }

                        if (current_function->return_type == FUNCTION_TYPE_INT)
                        {
                            result = get_result_int (&r);
                        }
                        else if (current_function->return_type == FUNCTION_TYPE_BYTE)
                        {
                            result = (unsigned char) get_result_int (&r);
                        }
                        else
                        {
                            char        tmp[32];
                            int         new_slot;
                            STRING *    t;
                            RESULT      r_idx;

                            new_slot = new_tmp_stringslot ((unsigned char *) NULL);
                            t = tmp_stringslots[new_slot];

                            switch (r.result_type)
                            {
                                case OPERAND_INT_CONSTANT:
                                    sprintf (tmp, "%d", r.result);
                                    copy_str2string (t, (unsigned char *) tmp);
                                    break;
                                case OPERAND_STRING_CONSTANT:
                                    copy_string2string (t, stringslots[r.result]);
                                    break;

                                case OPERAND_TEMP_STRING_CONSTANT:
                                {
                                    STRING * temp = t;

                                    tmp_stringslots[new_slot] = tmp_stringslots[r.result];
                                    tmp_stringslots[r.result] = temp;

                                    // reset flag after exchange!
                                    if (tmp_stringslots[r.result]->flags & STRING_FLAG_TEMP_ACTIVE)
                                    {
                                        tmp_stringslots[r.result]->flags &= ~STRING_FLAG_TEMP_ACTIVE;
                                    }
                                    else
                                    {
                                        fprintf (stderr, "internal error in nici(): temp string [%d] '%s' is not marked as temp string (%d)\n",
                                                    r.result, tmp_stringslots[r.result]->str, __LINE__);
                                    }
                                    break;
                                }

                                case OPERAND_LOCAL_STRING_VARIABLE:
                                    copy_string2string (t, stringslots[current_function->local_string_variables[r.result]]);
                                    break;

                                case OPERAND_LOCAL_STRING_ARRAY_VARIABLE:
                                    if (evaluate_postfix_slot (r.result_postfix_slot, &r_idx) < 0)
                                    {
                                        return -1;
                                    }
                                    result_idx = get_result_int (&r_idx);

                                    if (result_idx >= 0 && result_idx < current_function->local_string_arraysizes[r.result])
                                    {
                                        copy_string2string (t, stringslots[current_function->local_string_array_variables[r.result][result_idx]]);
                                    }
                                    else
                                    {
                                        fprintf (stderr, "fatal error line %d: index %d of local string array[%d] is out of range (%d)\n",
                                                        statementp[st_idx].line, result_idx, current_function->local_string_arraysizes[r.result], __LINE__);
                                        exit (1);
                                    }
                                    break;

                                case OPERAND_GLOBAL_STRING_VARIABLE:
                                    copy_string2string (t, stringslots[global_string_variables[r.result]]);
                                    break;

                                case OPERAND_GLOBAL_STRING_ARRAY_VARIABLE:
                                    if (evaluate_postfix_slot (r.result_postfix_slot, &r_idx) < 0)
                                    {
                                        return -1;
                                    }
                                    result_idx = get_result_int (&r_idx);

                                    if (result_idx >= 0 && result_idx < global_string_array_variables[r.result].arraysize)
                                    {
                                        copy_string2string (t, stringslots[global_string_array_variables[r.result].slots[result_idx]]);
                                    }
                                    else
                                    {
                                        fprintf (stderr, "fatal error line %d: index %d of global string array[%d] is out of range (%d)\n",
                                                        statementp[st_idx].line, result_idx, global_string_array_variables[r.result].arraysize, __LINE__);
                                        exit (1);
                                    }
                                    break;
                                default:
                                    fprintf (stderr, "internal error in nici(): unknown result_type = %d (%d)\n", r.result_type, __LINE__);
                                    break;
                            }

                            result = new_slot;
                        }

                        fip->reti = result;
                    }
                }

                del_stringslots (current_function->local_string_variables_used);

                if (current_function->local_string_variables_used)
                {
                    local_string_variable_stack_used -= current_function->local_string_variables_used;
                }

                if (current_function->local_int_variables_used)
                {
                    local_int_variable_stack_used -= current_function->local_int_variables_used;
                }

                if (current_function->local_int_array_variables)
                {
                    for (i = 0; i < current_function->local_int_array_variables_used; i++)
                    {
                        alloc_free (__FILE__, __LINE__, new_function->local_int_array_variables[i]);
                    }
                    alloc_free (__FILE__, __LINE__, new_function->local_int_array_variables);
                }

                if (current_function->local_byte_variables_used)
                {
                    local_byte_variable_stack_used -= current_function->local_byte_variables_used;
                }

                if (current_function->local_byte_array_variables)
                {
                    for (i = 0; i < current_function->local_byte_array_variables_used; i++)
                    {
                        alloc_free (__FILE__, __LINE__, new_function->local_byte_array_variables[i]);
                    }
                    alloc_free (__FILE__, __LINE__, new_function->local_byte_array_variables);
                }

                if (current_function->local_string_array_variables)
                {
                    for (i = 0; i < current_function->local_string_array_variables_used; i++)
                    {
                        alloc_free (__FILE__, __LINE__, new_function->local_string_array_variables[i]);
                    }
                    alloc_free (__FILE__, __LINE__, new_function->local_string_array_variables);
                }

                current_function->local_int_array_variables     = save_local_int_array_variables;
                current_function->local_byte_array_variables    = save_local_byte_array_variables;
                current_function->local_string_array_variables  = save_local_string_array_variables;
                return OK;
                break;
            }

            default:
            {
                fprintf (stderr, "error line %d: unhandled statement %d\n", statementp[st_idx].line, statementp[st_idx].type);
                return -1;
            }
        }
    }

    fprintf (stderr, "nici(): invalid exit.\n");
    return OK;
}

#define NULLP       ((char *) NULL)

static char *
readnum (char * s, int * vp)
{
    int     value   = 0;
    int     neg     = 0;
    int     valid   = 0;

    if (*s == '-')
    {
        neg = 1;
        s++;
    }
    else
    {
        neg = 0;
    }

    while (*s >= '0' && *s <= '9')
    {
        value= 10 * value + (*s - '0');
        s++;
        valid = 1;
    }

    while (*s == ' ')
    {
        s++;
    }

    if (neg)
    {
        value = -value;
    }

    *vp = value;

    if (valid)
    {
        return s;
    }

    fprintf (stderr, "error: readnum failed\n");
    return NULLP;
}

static char linebuf[256];

static char *
readline (char * buf, int maxlen)
{
    int ch;
    int len = 1;                // 1 for terminator

    while ((ch = getc (fp)) != '\n')
    {
        if (ch != '\r')
        {
            if (len < maxlen)
            {
                *buf++ = ch;
                len++;
            }
        }
    }

    *buf = '\0';
    return buf;
}

static void
deallocate_data (void)
{
    int     idx;

    if (functions)
    {
        if (local_string_variable_stack)
        {
            alloc_free (__FILE__, __LINE__, local_string_variable_stack);
        }

        if (local_byte_variable_stack)
        {
            alloc_free (__FILE__, __LINE__, local_byte_variable_stack);
        }

        if (local_int_variable_stack)
        {
            alloc_free (__FILE__, __LINE__, local_int_variable_stack);
        }

        for (idx = 0; idx < functions_used; idx++)
        {
            alloc_free (__FILE__, __LINE__, functions[idx].argtypes);
            alloc_free (__FILE__, __LINE__, functions[idx].argvars);
        }

        alloc_free (__FILE__, __LINE__, functions);
    }

    if (global_int_array_variables)
    {
        for (idx = 0; idx < global_int_array_variables_used; idx++)
        {
            alloc_free (__FILE__, __LINE__, global_int_array_variables[idx].values);
        }

        alloc_free (__FILE__, __LINE__, global_int_array_variables);
    }

    if (global_byte_array_variables)
    {
        for (idx = 0; idx < global_byte_array_variables_used; idx++)
        {
            alloc_free (__FILE__, __LINE__, global_byte_array_variables[idx].values);
        }

        alloc_free (__FILE__, __LINE__, global_byte_array_variables);
    }

    if (global_string_array_variables)
    {
        for (idx = 0; idx < global_string_array_variables_used; idx++)
        {
            alloc_free (__FILE__, __LINE__, global_string_array_variables[idx].slots);
        }

        alloc_free (__FILE__, __LINE__, global_string_array_variables);
    }

    if (global_string_variables)
    {
        alloc_free (__FILE__, __LINE__, global_string_variables);
    }

    if (global_byte_variables)
    {
        alloc_free (__FILE__, __LINE__, global_byte_variables);
    }

    if (global_int_variables)
    {
        alloc_free (__FILE__, __LINE__, global_int_variables);
    }

    if (fip_run_slots)
    {
        for (idx = 0; idx < fipslots_used; idx++)
        {
            if (fip_run_slots[idx])
            {
                if (fip_run_slots[idx]->postfix_slotp)
                {
                    alloc_free (__FILE__, __LINE__, fip_run_slots[idx]->postfix_slotp);
                }

                alloc_free (__FILE__, __LINE__, fip_run_slots[idx]);
            }
        }

        alloc_free (__FILE__, __LINE__, fip_run_slots);
    }

    if (postfix_slots)
    {
        for (idx = 0; idx < postfix_slots_used; idx++)
        {
            if (postfix_slots[idx])
            {
                alloc_free (__FILE__, __LINE__, postfix_slots[idx]);
            }
        }

        alloc_free (__FILE__, __LINE__, postfix_hint);
        alloc_free (__FILE__, __LINE__, postfix_depth);
        alloc_free (__FILE__, __LINE__, postfix_slots);
    }

    if (statementp)
    {
        alloc_free (__FILE__, __LINE__, statementp);
    }
}

static int
load_statements (void)
{
    char *  nextp;
    int     idx;

    func = nici_functions;

    if (! readline (linebuf, 256))
    {
        return -1;
    }

    if ((nextp = readnum (linebuf, &statements_used)) == NULLP)
    {
        return -1;
    }

    if ((statementp = alloc_malloc (__FILE__, __LINE__, statements_used * sizeof (STATEMENT))) == NULL)
    {
        fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
        return -1;
    }

    for (idx = 0; idx < statements_used; idx++)
    {
        if (! readline (linebuf, 256))
        {
            return -1;
        }

        nextp = linebuf;

        if ((nextp = readnum (nextp, &(statementp[idx].line))) == NULLP)
        {
            return -1;
        }

        if ((nextp = readnum (nextp, &(statementp[idx].type))) == NULLP)
        {
            return -1;
        }

        if ((nextp = readnum (nextp, &(statementp[idx].next))) == NULLP)
        {
            return -1;
        }

        switch (statementp[idx].type)
        {
            case STATEMENT_TYPE_INCREMENT:
            {
                STATEMENT_INCREMENT * stp = &(statementp[idx].st.st_increment);

                if ((nextp = readnum (nextp, &(stp->variable_idx))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->variable_type))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->step))) == NULLP)
                {
                    return -1;
                }

                break;
            }

            case STATEMENT_TYPE_INTERN_FUNCTION:
            {
                STATEMENT_INTERN_FUNCTION * stp = &(statementp[idx].st.st_intern_function);

                if ((nextp = readnum (nextp, &(stp->assignment_variable_idx))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->assignment_variable_type))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->assignment_variable_pslot))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->postfix_slot))) == NULLP)
                {
                    return -1;
                }

                break;
            }

            case STATEMENT_TYPE_IF:
            {
                STATEMENT_IF * stp = &(statementp[idx].st.st_if);

                if ((nextp = readnum (nextp, &(stp->postfix_slot1))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->operator))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->postfix_slot2))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->false_idx))) == NULLP)
                {
                    return -1;
                }

                break;
            }

            case STATEMENT_TYPE_ENDIF:
            {
                break;                                                                                      // nothing to do
            }

            case STATEMENT_TYPE_WHILE:
            {
                STATEMENT_WHILE * stp = &(statementp[idx].st.st_while);

                if ((nextp = readnum (nextp, &(stp->postfix_slot1))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->operator))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->postfix_slot2))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->endwhile_idx))) == NULLP)
                {
                    return -1;
                }

                break;
            }

            case STATEMENT_TYPE_ENDWHILE:
            {
                STATEMENT_ENDWHILE * stp = &(statementp[idx].st.st_endwhile);

                if ((nextp = readnum (nextp, &(stp->while_idx))) == NULLP)
                {
                    return -1;
                }

                break;
            }

            case STATEMENT_TYPE_FOR:
            {
                STATEMENT_FOR * stp = &(statementp[idx].st.st_for);

                if ((nextp = readnum (nextp, &(stp->for_variable_idx))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->for_variable_type))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->postfix_slot_start))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->postfix_slot_stop))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->postfix_slot_step))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->endfor_idx))) == NULLP)
                {
                    return -1;
                }

                break;
            }

            case STATEMENT_TYPE_ENDFOR:
            {
                STATEMENT_ENDFOR * stp = &(statementp[idx].st.st_endfor);

                if ((nextp = readnum (nextp, &(stp->for_idx))) == NULLP)
                {
                    return -1;
                }

                break;
            }

            case STATEMENT_TYPE_REPEAT:
            {
                STATEMENT_REPEAT * stp = &(statementp[idx].st.st_repeat);

                if ((nextp = readnum (nextp, &(stp->postfix_slot))) == NULLP)
                {
                    return -1;
                }

                if ((nextp = readnum (nextp, &(stp->endrepeat_idx))) == NULLP)
                {
                    return -1;
                }

                break;
            }

            case STATEMENT_TYPE_ENDREPEAT:
            {
                STATEMENT_ENDREPEAT * stp = &(statementp[idx].st.st_endrepeat);

                if ((nextp = readnum (nextp, &(stp->repeat_idx))) == NULLP)
                {
                    return -1;
                }

                break;
            }

            case STATEMENT_TYPE_LOOP:
            {
                STATEMENT_LOOP * stp = &(statementp[idx].st.st_loop);

                if ((nextp = readnum (nextp, &(stp->endloop_idx))) == NULLP)
                {
                    return -1;
                }

                break;
            }

            case STATEMENT_TYPE_ENDLOOP:
            {
                STATEMENT_ENDLOOP * stp = &(statementp[idx].st.st_endloop);

                if ((nextp = readnum (nextp, &(stp->loop_idx))) == NULLP)
                {
                    return -1;
                }

                break;
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

                if ((nextp = readnum (nextp, &(stp->postfix_slot))) == NULLP)
                {
                    return -1;
                }

                break;
            }

            default:
            {
                fprintf (stderr, "error line %d: unhandled statement %d\n", statementp[idx].line, statementp[idx].type);
                return -1;
            }
        }
    }

    return OK;
}


static int
load_postfix_slots (void)
{
    POSTFIX_ELEMENT *   p;
    char *              nextp;
    int                 depth;
    int                 hint;
    int                 idx;
    int                 d;

    if (! readline (linebuf, 256))
    {
        return -1;
    }

    if ((nextp = readnum (linebuf, &postfix_slots_used)) == NULLP)
    {
        return -1;
    }

    if (postfix_slots_used)
    {
        if ((postfix_slots = alloc_malloc (__FILE__, __LINE__, postfix_slots_used * sizeof (POSTFIX_ELEMENT *))) == NULL)
        {
            fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
            return -1;
        }

        if ((postfix_depth = alloc_malloc (__FILE__, __LINE__, postfix_slots_used * sizeof (int))) == NULL)
        {
            fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
            return -1;
        }

        if ((postfix_hint = alloc_malloc (__FILE__, __LINE__, postfix_slots_used * sizeof (int))) == NULL)
        {
            fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
            return -1;
        }
    }

    for (idx = 0; idx < postfix_slots_used; idx++)
    {
        if (! readline (linebuf, 256))
        {
            return -1;
        }

        nextp = linebuf;

        if ((nextp = readnum (nextp, &depth)) == NULLP)
        {
            return -1;
        }

        if ((nextp = readnum (nextp, &hint)) == NULLP)
        {
            return -1;
        }

        if (depth)
        {
            if ((postfix_slots[idx] = alloc_malloc (__FILE__, __LINE__, depth * sizeof (POSTFIX_ELEMENT))) == NULL)
            {
                fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
                return -1;
            }

            postfix_depth[idx]  = depth;
            postfix_hint[idx]   = hint;

            p = postfix_slots[idx];

            for (d = 0; d < depth; d++)
            {
                p[d].postfix_slot = -1;

                switch (*nextp)
                {
                    case 'o':                                                                           // operator
                    {
                        nextp++;
                        p[d].type   = OPERATOR;
                        p[d].value  = *nextp;
                        nextp++;
                        break;
                    }
                    case 'c':                                                                           // constant
                    {
                        nextp++;
                        p[d].type   = OPERAND_INT_CONSTANT;

                        if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                        {
                            return -1;
                        }
                        break;
                    }
                    case 'C':                                                                           // constant
                    {
                        nextp++;
                        p[d].type   = OPERAND_STRING_CONSTANT;

                        if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                        {
                            return -1;
                        }
                        break;
                    }
                    case 'v':                                                                           // local numeric variable
                    {
                        nextp++;
                        p[d].type   = OPERAND_LOCAL_INT_VARIABLE;

                        if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                        {
                            return -1;
                        }
                        break;
                    }
                    case 'V':                                                                           // global numeric variable
                    {
                        nextp++;
                        p[d].type   = OPERAND_GLOBAL_INT_VARIABLE;

                        if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                        {
                            return -1;
                        }
                        break;
                    }
                    case 'b':                                                                           // local byte variable
                    {
                        nextp++;
                        p[d].type   = OPERAND_LOCAL_BYTE_VARIABLE;

                        if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                        {
                            return -1;
                        }
                        break;
                    }
                    case 'B':                                                                           // global byte variable
                    {
                        nextp++;
                        p[d].type   = OPERAND_GLOBAL_BYTE_VARIABLE;

                        if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                        {
                            return -1;
                        }
                        break;
                    }
                    case 's':                                                                           // local string variable
                    {
                        nextp++;
                        p[d].type   = OPERAND_LOCAL_STRING_VARIABLE;

                        if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                        {
                            return -1;
                        }
                        break;
                    }
                    case 'S':                                                                           // global string variable
                    {
                        nextp++;
                        p[d].type   = OPERAND_GLOBAL_STRING_VARIABLE;

                        if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                        {
                            return -1;
                        }
                        break;
                    }
                    case 'a':                                                                           // arrays...
                    {
                        nextp++;

                        switch (*nextp)
                        {
                            case 's':
                                nextp++;
                                p[d].type   = OPERAND_LOCAL_STRING_ARRAY_VARIABLE;                      // local string variable array

                                if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                                {
                                    return -1;
                                }

                                if (*nextp != '[')
                                {
                                    return -1;
                                }

                                nextp++;

                                if ((nextp = readnum (nextp, &(p[d].postfix_slot))) == NULLP)
                                {
                                    return -1;
                                }

                                if (*nextp != ']')
                                {
                                    return -1;
                                }

                                nextp++;
                                break;
                            case 'S':
                                nextp++;
                                p[d].type   = OPERAND_GLOBAL_STRING_ARRAY_VARIABLE;                     // global string variable array

                                if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                                {
                                    return -1;
                                }

                                if (*nextp != '[')
                                {
                                    return -1;
                                }

                                nextp++;

                                if ((nextp = readnum (nextp, &(p[d].postfix_slot))) == NULLP)
                                {
                                    return -1;
                                }

                                if (*nextp != ']')
                                {
                                    return -1;
                                }

                                nextp++;
                                break;
                            case 'v':                                                                   // local numeric variable array
                            {
                                nextp++;
                                p[d].type   = OPERAND_LOCAL_INT_ARRAY_VARIABLE;

                                if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                                {
                                    return -1;
                                }

                                if (*nextp != '[')
                                {
                                    return -1;
                                }

                                nextp++;

                                if ((nextp = readnum (nextp, &(p[d].postfix_slot))) == NULLP)
                                {
                                    return -1;
                                }

                                if (*nextp != ']')
                                {
                                    return -1;
                                }

                                nextp++;
                                break;
                            }
                            case 'V':                                                                   // global numeric variable array
                            {
                                nextp++;
                                p[d].type   = OPERAND_GLOBAL_INT_ARRAY_VARIABLE;

                                if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                                {
                                    return -1;
                                }

                                if (*nextp != '[')
                                {
                                    return -1;
                                }

                                nextp++;

                                if ((nextp = readnum (nextp, &(p[d].postfix_slot))) == NULLP)
                                {
                                    return -1;
                                }

                                if (*nextp != ']')
                                {
                                    return -1;
                                }

                                nextp++;
                                break;
                            }
                            case 'b':                                                                   // local byte variable array
                            {
                                nextp++;
                                p[d].type   = OPERAND_LOCAL_BYTE_ARRAY_VARIABLE;

                                if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                                {
                                    return -1;
                                }

                                if (*nextp != '[')
                                {
                                    return -1;
                                }

                                nextp++;

                                if ((nextp = readnum (nextp, &(p[d].postfix_slot))) == NULLP)
                                {
                                    return -1;
                                }

                                if (*nextp != ']')
                                {
                                    return -1;
                                }

                                nextp++;
                                break;
                            }
                            case 'B':                                                                   // global byte variable array
                            {
                                nextp++;
                                p[d].type   = OPERAND_GLOBAL_BYTE_ARRAY_VARIABLE;

                                if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                                {
                                    return -1;
                                }

                                if (*nextp != '[')
                                {
                                    return -1;
                                }

                                nextp++;

                                if ((nextp = readnum (nextp, &(p[d].postfix_slot))) == NULLP)
                                {
                                    return -1;
                                }

                                if (*nextp != ']')
                                {
                                    return -1;
                                }

                                nextp++;
                                break;
                            }
                            default:
                            {
                                fprintf (stderr, "unhandled postfix array type: a'%c'\n", *nextp);
                                return -1;
                            }
                        }
                        break;
                    }
                    case 'f':                                                                           // function
                    {
                        nextp++;
                        p[d].type   = OPERAND_INTERN_FUNCTION;

                        if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                        {
                            return -1;
                        }
                        break;
                    }

                    case 'F':                                                                           // function
                    {
                        nextp++;
                        p[d].type   = OPERAND_EXTERN_FUNCTION;

                        if ((nextp = readnum (nextp, &(p[d].value))) == NULLP)
                        {
                            return -1;
                        }
                        break;
                    }

                    case '\n':
                    {
                        fprintf (stderr, "unexpected end of line: postfix depth = %d, d = %d\n", depth, d);
                        return -1;
                    }
                    default:
                    {
                        fprintf (stderr, "unhandled postfix type: '%c'\n", *nextp);
                        return -1;
                    }
                }
            }
        }
    }

    return OK;
}

static int
load_fip_run_slots (void)
{
    char *  nextp;
    int     idx;
    int     argi;

    if (! readline (linebuf, 256))
    {
        return -1;
    }

    if ((nextp = readnum (linebuf, &fipslots_used)) == NULLP)
    {
        return -1;
    }

    if (fipslots_used)
    {
        if ((fip_run_slots = alloc_malloc (__FILE__, __LINE__, fipslots_used * sizeof (FIP_RUN *))) == NULL)
        {
            fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
            return -1;
        }
    }

    for (idx = 0; idx < fipslots_used; idx++)
    {
        int argc;

        if (! readline (linebuf, 256))
        {
            return -1;
        }

        nextp = linebuf;

        if ((fip_run_slots[idx] = alloc_malloc (__FILE__, __LINE__, sizeof (FIP_RUN))) == NULL)
        {
            fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
            return -1;
        }

        if ((nextp = readnum (nextp, &(fip_run_slots[idx]->func_idx))) == NULLP)
        {
            return -1;
        }

        if ((nextp = readnum (nextp, &argc)) == NULLP)
        {
            return -1;
        }

        fip_run_slots[idx]->argc            = argc;

        if (argc)
        {
            fip_run_slots[idx]->postfix_slotp   = alloc_calloc (__FILE__, __LINE__, argc, sizeof (int));

            if (! fip_run_slots[idx]->postfix_slotp)
            {
                fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
                return -1;
            }

            for (argi = 0; argi < argc; argi++)
            {
                if ((nextp = readnum (nextp, &(fip_run_slots[idx]->postfix_slotp[argi]))) == NULLP)
                {
                    return -1;
                }
            }
        }
        else
        {
            fip_run_slots[idx]->postfix_slotp   = (int *) NULL;
        }
    }

    return OK;
}

int
load_strings (void)
{
    char *  nextp;
    int     slot;
    int     i;
    int     strings_used;

    if (! readline (linebuf, 256))
    {
        return -1;
    }

    nextp = linebuf;

    if ((nextp = readnum (nextp, &strings_used)) == NULLP)
    {
        return -1;
    }

    if (strings_used > 0)
    {
        for (i = 0; i < strings_used; i++)
        {
            if (! readline (linebuf, 256))
            {
                return -1;
            }

            slot = new_stringslot ((unsigned char *) linebuf);

            if (slot < 0)
            {
                fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
                return -1;
            }
        }
    }

    return OK;
}

int
load_variables (void)
{
    char *  nextp;
    int     i;
    int     v;

    if (! readline (linebuf, 256))
    {
        return -1;
    }

    nextp = linebuf;

    if ((nextp = readnum (nextp, &global_int_variables_used)) == NULLP)
    {
        return -1;
    }

    if (global_int_variables_used > 0)
    {
        global_int_variables = alloc_calloc (__FILE__, __LINE__, global_int_variables_used, sizeof (int));

        if (! global_int_variables)
        {
            fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
            return -1;
        }

        for (i = 0; i < global_int_variables_used; i++)
        {
            if (! readline (linebuf, 256))
            {
                return -1;
            }

            nextp = linebuf;

            if ((nextp = readnum (nextp, &v)) == NULLP)
            {
                return -1;
            }

            global_int_variables[i] = v;
        }
    }

    if (! readline (linebuf, 256))
    {
        return -1;
    }

    nextp = linebuf;

    if ((nextp = readnum (nextp, &global_byte_variables_used)) == NULLP)
    {
        return -1;
    }

    if (global_byte_variables_used > 0)
    {
        global_byte_variables = alloc_calloc (__FILE__, __LINE__, global_byte_variables_used, sizeof (uint8_t));

        if (! global_byte_variables)
        {
            fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
            return -1;
        }

        for (i = 0; i < global_byte_variables_used; i++)
        {
            if (! readline (linebuf, 256))
            {
                return -1;
            }

            nextp = linebuf;

            if ((nextp = readnum (nextp, &v)) == NULLP)
            {
                return -1;
            }

            global_byte_variables[i] = v;
        }
    }

    if (! readline (linebuf, 256))
    {
        return -1;
    }

    nextp = linebuf;

    if ((nextp = readnum (nextp, &global_string_variables_used)) == NULLP)
    {
        return -1;
    }

    if (global_string_variables_used > 0)
    {
        global_string_variables = alloc_calloc (__FILE__, __LINE__, global_string_variables_used, sizeof (int));

        if (! global_string_variables)
        {
            fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
            return -1;
        }

        for (i = 0; i < global_string_variables_used; i++)
        {
            int slot;

            if (! readline (linebuf, 256))
            {
                return -1;
            }

            slot = new_stringslot ((unsigned char *) NULL);
            global_string_variables[i] = slot;

            if (*linebuf)
            {
                copy_str2string (stringslots[global_string_variables[i]], (unsigned char *) linebuf);
            }
        }
    }

    return OK;
}

int
load_array_variables (void)
{
    char *  nextp;
    int     idx;
    int     arraysize;
    int     i;
    int     slot;

    if (! readline (linebuf, 256))
    {
        return -1;
    }

    nextp = linebuf;

    if ((nextp = readnum (nextp, &global_int_array_variables_used)) == NULLP)
    {
        return -1;
    }

    if (global_int_array_variables_used > 0)
    {
        global_int_array_variables = alloc_calloc (__FILE__, __LINE__, global_int_array_variables_used, sizeof (INT_ARRAY_VARIABLE));

        if (! global_int_array_variables)
        {
            fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
            return -1;
        }

        for (idx = 0; idx < global_int_array_variables_used; idx++)
        {
            if (! readline (linebuf, 256))
            {
                return -1;
            }

            nextp = linebuf;

            if ((nextp = readnum (nextp, &(global_int_array_variables[idx].arraysize))) == NULLP)
            {
                return -1;
            }

            global_int_array_variables[idx].values = alloc_calloc (__FILE__, __LINE__, global_int_array_variables[idx].arraysize, sizeof (int));

            if (! global_int_array_variables[idx].values)
            {
                fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
                return -1;
            }
        }
    }

    if (! readline (linebuf, 256))
    {
        return -1;
    }

    nextp = linebuf;

    if ((nextp = readnum (nextp, &global_byte_array_variables_used)) == NULLP)
    {
        return -1;
    }

    if (global_byte_array_variables_used > 0)
    {
        global_byte_array_variables = alloc_calloc (__FILE__, __LINE__, global_byte_array_variables_used, sizeof (BYTE_ARRAY_VARIABLE));

        if (! global_byte_array_variables)
        {
            fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
            return -1;
        }

        for (idx = 0; idx < global_byte_array_variables_used; idx++)
        {
            if (! readline (linebuf, 256))
            {
                return -1;
            }

            nextp = linebuf;

            if ((nextp = readnum (nextp, &(global_byte_array_variables[idx].arraysize))) == NULLP)
            {
                return -1;
            }

            global_byte_array_variables[idx].values = alloc_calloc (__FILE__, __LINE__, global_byte_array_variables[idx].arraysize, sizeof (uint8_t));

            if (! global_byte_array_variables[idx].values)
            {
                fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
                return -1;
            }
        }
    }

    if (! readline (linebuf, 256))
    {
        return -1;
    }

    nextp = linebuf;

    if ((nextp = readnum (nextp, &global_string_array_variables_used)) == NULLP)
    {
        return -1;
    }

    if (global_string_array_variables_used > 0)
    {
        global_string_array_variables = alloc_calloc (__FILE__, __LINE__, global_string_array_variables_used, sizeof (STRING_ARRAY_VARIABLE));

        if (! global_string_array_variables)
        {
            fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
            return -1;
        }

        for (idx = 0; idx < global_string_array_variables_used; idx++)
        {
            if (! readline (linebuf, 256))
            {
                return -1;
            }

            nextp = linebuf;

            if ((nextp = readnum (nextp, &(global_string_array_variables[idx].arraysize))) == NULLP)
            {
                return -1;
            }

            arraysize = global_string_array_variables[idx].arraysize;

            global_string_array_variables[idx].slots = alloc_calloc (__FILE__, __LINE__, arraysize, sizeof (int));

            if (! global_string_array_variables[idx].slots)
            {
                fprintf (stderr, "error: out of memory (%d)\n", __LINE__);
                return -1;
            }

            for (i = 0; i < arraysize; i++)
            {
                slot = new_stringslot ((unsigned char *) NULL);
                global_string_array_variables[idx].slots[i] = slot;
            }
        }
    }

    return OK;
}

int
load_functions (void)
{
    char *  nextp;
    int     i;
    int     j;
    int     first_statement_idx;
    int     return_type;
    int     argc;
    int     argvar;

    if (! readline (linebuf, 256))
    {
        return -1;
    }

    nextp = linebuf;

    if ((nextp = readnum (nextp, &functions_used)) == NULLP)
    {
        return -1;
    }

    functions = alloc_malloc (__FILE__, __LINE__, functions_used * sizeof (FUNCTION));

    for (i = 0; i < functions_used; i++)
    {
        if (! readline (linebuf, 256))
        {
            return -1;
        }

        nextp = linebuf;

        if ((nextp = readnum (nextp, &first_statement_idx)) == NULLP)
        {
            return -1;
        }

        if ((nextp = readnum (nextp, &return_type)) == NULLP)
        {
            return -1;
        }

        if ((nextp = readnum (nextp, &argc)) == NULLP)
        {
            return -1;
        }

        functions[i].first_statement_idx    = first_statement_idx;
        functions[i].return_type            = return_type;
        functions[i].argc                   = argc;

        if (argc)
        {
            functions[i].argtypes               = alloc_malloc (__FILE__, __LINE__, argc * sizeof (int));
            functions[i].argvars                = alloc_malloc (__FILE__, __LINE__, argc * sizeof (int));

            for (j = 0; j < argc; j++)
            {
                if (*nextp == 'i')
                {
                    functions[i].argtypes[j] = ARGUMENT_TYPE_INT;
                }
                else if (*nextp == 'b')
                {
                    functions[i].argtypes[j] = ARGUMENT_TYPE_BYTE;
                }
                else if (*nextp == 's')
                {
                    functions[i].argtypes[j] = ARGUMENT_TYPE_STRING;
                }
                else
                {
                    fprintf (stderr, "error: invalid argument type: '%c'\n", *nextp);
                    return -1;
                }

                nextp++;

                if ((nextp = readnum (nextp, &argvar)) == NULLP)
                {
                    return -1;
                }

                functions[i].argvars[j] = argvar;
            }
        }
        else
        {
            functions[i].argtypes               = (int *) NULL;
            functions[i].argvars                = (int *) NULL;
        }

        if (! readline (linebuf, 256))
        {
            return -1;
        }

        nextp = linebuf;

        if ((nextp = readnum (nextp, &functions[i].local_int_variables_used)) == NULLP)
        {
            return -1;
        }

        if ((nextp = readnum (nextp, &functions[i].local_byte_variables_used)) == NULLP)
        {
            return -1;
        }

        if ((nextp = readnum (nextp, &functions[i].local_string_variables_used)) == NULLP)
        {
            return -1;
        }

        if (! readline (linebuf, 256))
        {
            return -1;
        }

        nextp = linebuf;

        if ((nextp = readnum (nextp, &functions[i].local_int_array_variables_used)) == NULLP)
        {
            return -1;
        }

        functions[i].local_int_arraysizes = malloc (functions[i].local_int_array_variables_used * sizeof (int));

        for (j = 0; j < functions[i].local_int_array_variables_used; j++)
        {
            if (! readline (linebuf, 256))
            {
                return -1;
            }

            nextp = linebuf;

            if ((nextp = readnum (nextp, &functions[i].local_int_arraysizes[j])) == NULLP)
            {
                return -1;
            }
        }

        if (! readline (linebuf, 256))
        {
            return -1;
        }

        nextp = linebuf;

        if ((nextp = readnum (nextp, &functions[i].local_byte_array_variables_used)) == NULLP)
        {
            return -1;
        }

        functions[i].local_byte_arraysizes = malloc (functions[i].local_byte_array_variables_used * sizeof (int));

        for (j = 0; j < functions[i].local_byte_array_variables_used; j++)
        {
            if (! readline (linebuf, 256))
            {
                return -1;
            }

            nextp = linebuf;

            if ((nextp = readnum (nextp, &functions[i].local_byte_arraysizes[j])) == NULLP)
            {
                return -1;
            }
        }

        if (! readline (linebuf, 256))
        {
            return -1;
        }

        nextp = linebuf;

        if ((nextp = readnum (nextp, &functions[i].local_string_array_variables_used)) == NULLP)
        {
            return -1;
        }

        functions[i].local_string_arraysizes = malloc (functions[i].local_string_array_variables_used * sizeof (int));

        for (j = 0; j < functions[i].local_string_array_variables_used; j++)
        {
            if (! readline (linebuf, 256))
            {
                return -1;
            }

            nextp = linebuf;

            if ((nextp = readnum (nextp, &functions[i].local_string_arraysizes[j])) == NULLP)
            {
                return -1;
            }
        }
    }

    if (! readline (linebuf, 256))
    {
        return -1;
    }

    nextp = linebuf;

    if ((nextp = readnum (nextp, &main_function_idx)) == NULLP)
    {
        return -1;
    }

    local_int_variable_stack = alloc_malloc (__FILE__, __LINE__, LOCAL_VARIABLE_STACK_ALLOC_GRANULARITY * sizeof (int));
    local_int_variable_stack_allocated = LOCAL_VARIABLE_STACK_ALLOC_GRANULARITY;

    local_byte_variable_stack = alloc_malloc (__FILE__, __LINE__, LOCAL_VARIABLE_STACK_ALLOC_GRANULARITY * sizeof (int));
    local_byte_variable_stack_allocated = LOCAL_VARIABLE_STACK_ALLOC_GRANULARITY;

    local_string_variable_stack = alloc_malloc (__FILE__, __LINE__, LOCAL_VARIABLE_STACK_ALLOC_GRANULARITY * sizeof (int));
    local_string_variable_stack_allocated = LOCAL_VARIABLE_STACK_ALLOC_GRANULARITY;

    return OK;
}


static int
nic_load (void)
{
    int     rtc = -1;

    if (load_statements ()      == OK &&
        load_postfix_slots ()   == OK &&
        load_fip_run_slots ()   == OK &&
        load_strings ()         == OK &&
        load_variables ()       == OK &&
        load_array_variables () == OK &&
        load_functions ()       == OK)
    {
        rtc = OK;
    }

    return rtc;
}

#if defined (unix) || defined (WIN32)
#define cmd_nic     main
#endif

int
cmd_nic (int argc, const char ** argv)
{
    const char *    pgm = argv[0];
    int             verbose = 0;

    while (argc > 2)
    {
        if (!strcmp (argv[1], "-v"))
        {
            verbose = 1;
            argc--;
            argv++;
        }
        else
        {
            break;
        }
    }

    if (argc >= 2)
    {
        const char * fname = argv[1];

        fp = fopen (fname, "r");

        if (fp)
        {
            int rtc = nic_load ();
            fclose (fp);

            if (rtc == OK)
            {
                FUNCTION *  func = functions + main_function_idx;

                main_argc = argc - 2;
                main_argv = argv + 2;

                if (func->argc != main_argc)
                {
                    fprintf (stderr, "error: %s needs exactly %d argument%s\n", fname, func->argc, func->argc == 1 ? "" : "s");
                    rtc = 1;
                }
                else
                {
#if unix
                    signal (SIGINT, mysighandler);
#else
                    console_set_rawmode (FALSE);
#endif
                    rtc = nici (main_function_idx, (FIP_RUN *) NULL);

                    nici_alarm_reset_all ();
#if unix
                    signal (SIGINT, SIG_DFL);
#else
                    console_set_rawmode (TRUE);
#endif

                    if (verbose)
                    {
                        string_statistics ();
                    }

                    nici_file_close_all_open_files ();
                    tft_reset_font ();
                }

                deallocate_strings ();
                deallocate_data ();
                alloc_list ();
                alloc_free_holes ();

                if (rtc == OK)
                {
                    return 0;
                }
                else if (rtc < 0)
                {
                    fprintf (stderr, "Interrupted\n");
                }
            }
        }
        else
        {
#ifdef unix
            perror (argv[1]);
#else
            fprintf (stderr, "%s: cannot open\n", argv[1]);
#endif
        }
    }
    else
    {
        fprintf (stderr, "usage: %s [-v] file.n\n", pgm);
    }

    return 1;
}
