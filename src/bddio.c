/*========================================================================
               Copyright (C) 1996-2002 by Jorn Lind-Nielsen
                            All rights reserved

    Permission is hereby granted, without written agreement and without
    license or royalty fees, to use, reproduce, prepare derivative
    works, distribute, and display this software and its documentation
    for any purpose, provided that (1) the above copyright notice and
    the following two paragraphs appear in all copies of the source code
    and (2) redistributions, including without limitation binaries,
    reproduce these notices in the supporting documentation. Substantial
    modifications to this software may be copyrighted by their authors
    and need not follow the licensing terms described here, provided
    that the new terms are clearly indicated in all files where they apply.

    IN NO EVENT SHALL JORN LIND-NIELSEN, OR DISTRIBUTORS OF THIS
    SOFTWARE BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, SPECIAL,
    INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS
    SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE AUTHORS OR ANY OF THE
    ABOVE PARTIES HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    JORN LIND-NIELSEN SPECIFICALLY DISCLAIM ANY WARRANTIES, INCLUDING,
    BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
    FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS
    ON AN "AS IS" BASIS, AND THE AUTHORS AND DISTRIBUTORS HAVE NO
    OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
    MODIFICATIONS.
========================================================================*/

/*************************************************************************
  $Header: /cvsroot/buddy/buddy/src/bddio.c,v 1.1.1.1 2004/06/25 13:22:14 haimcohen Exp $
  FILE:  bddio.c
  DESCR: File I/O routines for BDD package
  AUTH:  Jorn Lind
  DATE:  (C) june 1997
*************************************************************************/
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/stat.h>
#include "bdd.h"
#include "kernel.h"
#include <stdbool.h>


static void bdd_printset_rec(FILE *, int, int *);
static void bdd_fprintdot_rec(FILE*, BDD);
static int  bdd_save_rec(FILE*, int);
static int  bdd_loaddata(FILE *);
static int  loadhash_get(int);
static void loadhash_add(int, int);
static int  loadhash_realloc();
static int  unresolved_realloc();
static void add_unresolved(int, int, int, int);
static void resolve_nodes(int *);
static bddfilehandler filehandler;

typedef struct s_LoadHash
{
   int key;
   int data;
   int first;
   int next;
} LoadHash;

typedef struct unresolved_node{
   int key;
   int high;
   int low;
   int var;
}unresolved_node;

#define LH_INIT_VALUE           10    // initial size of the hash table
#define LH_INIT_REALLOC_FACTOR  0.75

static bool nameR,typeR,varsR,nodesR,rootR,orderR = false; 
static LoadHash *lh_table;
static int       lh_freepos;
static int       lh_nodenum        = LH_INIT_VALUE;    // current size of the hashtable (if not specified, LH_INIT_VALUE)
static float     lh_realloc_factor = LH_INIT_REALLOC_FACTOR; // if nodenum is known,changes to 1 (realloc propably won't be necessary)
static int       lh_count          = 0; // current number of nodes in hashtable
static int      *loadvar2level;
static unresolved_node *un_nodes   = NULL;
static int       unresolved        = 0;
static int       unresolved_size   = LH_INIT_VALUE;

/*=== PRINTING ========================================================*/


/*
NAME    {* bdd\_file\_hook *}
SECTION {* kernel *}
SHORT   {* Specifies a printing callback handler *}
PROTO   {* bddfilehandler bdd_file_hook(bddfilehandler handler) *}
DESCR   {* A printing callback handler for use with BDDs is used to
           convert the BDD variable number into something readable by the
	   end user. Typically the handler will print a string name
	   instead of the number. A handler could look like this:
	   \begin{verbatim}
void printhandler(FILE *o, int var)
{
   extern char **names;
   fprintf(o, "%s", names[var]);
}
\end{verbatim}

           \noindent
           The handler can then be passed to BuDDy like this:
	   {\tt bdd\_file\_hook(printhandler)}.

	   No default handler is supplied. The argument {\tt handler} may be
	   NULL if no handler is needed. *}
RETURN  {* The old handler *}
ALSO    {* bdd\_printset, bdd\_strm\_hook, fdd\_file\_hook *}
*/
bddfilehandler bdd_file_hook(bddfilehandler handler)
{
   bddfilehandler old = filehandler;
   filehandler = handler;
   return old;
}


/*
NAME    {* bdd\_printall *}
EXTRA   {* bdd\_fprintall *}
SECTION {* fileio *}
SHORT   {* prints all used entries in the node table *}
PROTO   {* void bdd_printall(void)
void bdd_fprintall(FILE* ofile) *}
DESCR   {* Prints to either stdout or the file {\tt ofile} all the used
           entries in the main node table. The format is:
	   \begin{Ill}
  	     {\tt [Nodenum] Var/level Low High}
	   \end{Ill}
	   Where {\tt Nodenum} is the position in the node table and level
	   is the position in the current variable order. *}
ALSO    {* bdd\_printtable, bdd\_printset, bdd\_printdot *}
*/
void bdd_printall(void)
{
   bdd_fprintall(stdout);
}


void bdd_fprintall(FILE *ofile)
{
   int n;

   for (n=0 ; n<bddnodesize ; n++)
   {
      if (LOW(n) != -1)
      {
	 fprintf(ofile, "[%5d - %2d] ", n, bddnodes[n].refcou);
	 if (filehandler)
	    filehandler(ofile, bddlevel2var[LEVEL(n)]);
	 else
	    fprintf(ofile, "%3d", bddlevel2var[LEVEL(n)]);

	 fprintf(ofile, ": %3d", LOW(n));
	 fprintf(ofile, " %3d", HIGH(n));
	 fprintf(ofile, "\n");
      }
   }
}


/*
NAME    {* bdd\_printtable *}
EXTRA   {* bdd\_fprinttable *}
SECTION {* fileio *}
SHORT   {* prints the node table entries used by a BDD *}
PROTO   {* void bdd_printtable(BDD r)
void bdd_fprinttable(FILE* ofile, BDD r) *}
DESCR   {* Prints to either stdout or the file {\tt ofile} all the entries
           in the main node table used by {\tt r}. The format is:
	   \begin{Ill}
  	     {\tt [Nodenum] Var/level :  Low High}
	   \end{Ill}
	   Where {\tt Nodenum} is the position in the node table and level
	   is the position in the current variable order. *}
ALSO    {* bdd\_printall, bdd\_printset, bdd\_printdot *}
*/
void bdd_printtable(BDD r)
{
   bdd_fprinttable(stdout, r);
}


void bdd_fprinttable(FILE *ofile, BDD r)
{
   BddNode *node;
   int n;

   fprintf(ofile, "ROOT: %d\n", r);
   if (r < 2)
      return;

   bdd_mark(r);

   for (n=0 ; n<bddnodesize ; n++)
   {
      if (LEVEL(n) & MARKON)
      {
	 node = &bddnodes[n];

	 LEVELp(node) &= MARKOFF;

	 fprintf(ofile, "[%5d] ", n);
	 if (filehandler)
	    filehandler(ofile, bddlevel2var[LEVELp(node)]);
	 else
	    fprintf(ofile, "%3d", bddlevel2var[LEVELp(node)]);

	 fprintf(ofile, ": %3d", LOWp(node));
	 fprintf(ofile, " %3d", HIGHp(node));
	 fprintf(ofile, "\n");
      }
   }
}


/*
NAME    {* bdd\_printset *}
EXTRA   {* bdd\_fprintset *}
SECTION {* fileio *}
SHORT   {* prints the set of truth assignments specified by a BDD *}
PROTO   {* bdd_printset(BDD r)
bdd_fprintset(FILE* ofile, BDD r) *}
DESCR   {* Prints all the truth assignments for {\tt r} that would yield
           it true. The format is:
	   \begin{Ill}
	     {\tt < $x_{1,1}:c_{1,1},\ldots,x_{1,n_1}:c_{1,n_1}$ >\\
	          < $x_{2,1}:c_{2,1},\ldots,x_{2,n_2}:c_{2,n_2}$ >\\
		  $\ldots$ \\
	          < $x_{N,1}:c_{N,1},\ldots,x_{N,n_3}:c_{N,n_3}$ > }
	   \end{Ill}
	   Where the $x$'s are variable numbers (and the position in the
	   current order) and the $c$'s are the
	   possible assignments to these. Each set of brackets designates
	   one possible assignment to the set of variables that make up the
	   BDD. All variables not shown are don't cares. It is possible to
	   specify a callback handler for printing of the variables using
	   {\tt bdd\_file\_hook} or {\tt bdd\_strm\_hook}. *}
ALSO    {* bdd\_printall, bdd\_printtable, bdd\_printdot, bdd\_file\_hook, bdd\_strm\_hook *}
*/
void bdd_printset(BDD r)
{
   bdd_fprintset(stdout, r);
}


void bdd_fprintset(FILE *ofile, BDD r)
{
   int *set;

   if (r < 2)
   {
      fprintf(ofile, "%s", r == 0 ? "F" : "T");
      return;
   }

   if ((set=(int *)malloc(sizeof(int)*bddvarnum)) == NULL)
   {
      bdd_error(BDD_MEMORY);
      return;
   }

   memset(set, 0, sizeof(int) * bddvarnum);
   bdd_printset_rec(ofile, r, set);
   free(set);
}


static void bdd_printset_rec(FILE *ofile, int r, int *set)
{
   int n;
   int first;

   if (r == 0)
      return;
   else
   if (r == 1)
   {
      fprintf(ofile, "<");
      first = 1;

      for (n=0 ; n<bddvarnum ; n++)
      {
	 if (set[n] > 0)
	 {
	    if (!first)
	       fprintf(ofile, ", ");
	    first = 0;
	    if (filehandler)
	       filehandler(ofile, bddlevel2var[n]);
	    else
	       fprintf(ofile, "%d", bddlevel2var[n]);
	    fprintf(ofile, ":%d", (set[n]==2 ? 1 : 0));
	 }
      }

      fprintf(ofile, ">");
   }
   else
   {
      set[LEVEL(r)] = 1;
      bdd_printset_rec(ofile, LOW(r), set);

      set[LEVEL(r)] = 2;
      bdd_printset_rec(ofile, HIGH(r), set);

      set[LEVEL(r)] = 0;
   }
}


/*
NAME    {* bdd\_printdot *}
EXTRA   {* bdd\_fprintdot *}
SECTION {* fileio *}
SHORT   {* prints a description of a BDD in DOT format *}
PROTO   {* void bdd_printdot(BDD r)
int bdd_fnprintdot(char* fname, BDD r)
void bdd_fprintdot(FILE* ofile, BDD r) *}
DESCR   {* Prints a BDD in a format suitable for use with the graph
           drawing program DOT to either stdout, a designated file
	   {\tt ofile} or the file named by {\tt fname}. In the last case
	   the file will be opened for writing, any previous contents
	   destroyed and then closed again. *}
ALSO    {* bdd\_printall, bdd\_printtable, bdd\_printset *}
*/
void bdd_printdot(BDD r)
{
   bdd_fprintdot(stdout, r);
}


int bdd_fnprintdot(char *fname, BDD r)
{
   FILE *ofile = fopen(fname, "w");
   if (ofile == NULL)
      return bdd_error(BDD_FILE);
   bdd_fprintdot(ofile, r);
   fclose(ofile);
   return 0;
}


void bdd_fprintdot(FILE* ofile, BDD r)
{
   fprintf(ofile, "digraph G {\n");
   fprintf(ofile, "0 [shape=box, label=\"0\", style=filled, shape=box, height=0.3, width=0.3];\n");
   fprintf(ofile, "1 [shape=box, label=\"1\", style=filled, shape=box, height=0.3, width=0.3];\n");

   bdd_fprintdot_rec(ofile, r);

   fprintf(ofile, "}\n");

   bdd_unmark(r);
}


static void bdd_fprintdot_rec(FILE* ofile, BDD r)
{
   if (ISCONST(r) || MARKED(r))
      return;

   fprintf(ofile, "%d [label=\"", r);
   if (filehandler)
      filehandler(ofile, bddlevel2var[LEVEL(r)]);
   else
      fprintf(ofile, "%d", bddlevel2var[LEVEL(r)]);
   fprintf(ofile, "\"];\n");

   fprintf(ofile, "%d -> %d [style=dotted];\n", r, LOW(r));
   fprintf(ofile, "%d -> %d [style=filled];\n", r, HIGH(r));

   SETMARK(r);

   bdd_fprintdot_rec(ofile, LOW(r));
   bdd_fprintdot_rec(ofile, HIGH(r));
}


/*=== SAVE =============================================================*/

/*
NAME    {* bdd\_save *}
EXTRA   {* bdd\_fnsave *}
SECTION {* fileio *}
SHORT   {* saves a BDD to a file *}
PROTO   {* int bdd_fnsave(char *fname, BDD r)
int bdd_save(FILE *ofile, BDD r) *}
DESCR   {* Saves the nodes used by {\tt r} to either a file {\tt ofile}
           which must be opened for writing or to the file named {\tt fname}.
	   In the last case the file will be truncated and opened for
	   writing. *}
ALSO    {* bdd\_load *}
RETURN  {* Zero on succes, otherwise an error code from {\tt bdd.h}. *}
*/
int bdd_fnsave(char *fname, BDD r, char *bddname, char *bddtype)
{
   FILE *ofile;
   int ok;

   if ((ofile=fopen(fname,"w")) == NULL)
      return bdd_error(BDD_FILE);
   fprintf(ofile, "@%s\n%%Name %s\n", bddtype, bddname);
   ok = bdd_save(ofile, r);
   fclose(ofile);
   return ok;
}


int bdd_save(FILE *ofile, BDD r)
{
   int err, n=0;

   if (r < 2)
   {
      fprintf(ofile, "%%Vars 0\n%%Nodes 0\n%%Root %d\n", r);
      return 0;
   }

   bdd_markcount(r, &n);
   bdd_unmark(r);
   fprintf(ofile, "%%Vars %d\n%%Nodes %d\n%%Root %d\n", bddvarnum, n, r);

   fprintf(ofile, "%%Ordering ");
   for (n=0 ; n<bddvarnum ; n++)
      fprintf(ofile, "%d ", bddvar2level[n]);
   fprintf(ofile, "\n");

   err = bdd_save_rec(ofile, r);
   bdd_unmark(r);

   return err;
}


static int bdd_save_rec(FILE *ofile, int root)
{
   BddNode *node = &bddnodes[root];
   int err;

   if (root < 2)
      return 0;

   if (LEVELp(node) & MARKON)
      return 0;
   LEVELp(node) |= MARKON;

   if ((err=bdd_save_rec(ofile, LOWp(node))) < 0)
      return err;
   if ((err=bdd_save_rec(ofile, HIGHp(node))) < 0)
      return err;

   fprintf(ofile, "%d[%d] %d %d\n",
	   root, bddlevel2var[LEVELp(node) & MARKHIDE], LOWp(node), HIGHp(node));

   return 0;
}


/*=== LOAD =============================================================*/

/*
NAME    {* bdd\_load *}
EXTRA   {* bdd\_fnload *}
SECTION {* fileio *}
SHORT   {* loads a BDD from a file *}
PROTO   {* int bdd_fnload(char *fname, BDD *r)
int bdd_load(FILE *ifile, BDD *r) *}
DESCR   {* Loads a BDD from a file into the BDD pointed to by {\tt r}.
           The file can either be the file {\tt ifile} which must be opened
	   for reading or the file named {\tt fname} which will be opened
	   automatically for reading.

      The input file consists of these mandatory parts:

      @TYPE                  (where TYPE represents type of the bdd representation, marks the start of representation)
      %Name name
      %Vars v                (where v is the number of variables)
      %Nodes n               (where n is the number of nodes)
      %Root r                (where r is the id number of the root (0/1 if constant))

      %Ordering a0 a1 ... av (where a0...av are variable numbers in their ordering, this part is not mandatory if v=n=0 -> constant)     

      Now the representation of each node follows:

      id[var] low high       (where id is id number of the node, var is the variable number, low and high are ids of low and high children)

      Representation can include comments which will be ignored, starting with # until the end of line.

	   The nodes {\it must} be saved in a order such that any low or
	   high node must be defined before it is mentioned. *}
ALSO    {* bdd\_save *}
RETURN  {* Zero on succes, otherwise an error code from {\tt bdd.h}. *}
*/
int bdd_fnload(char *fname, BDD *root)
{
   FILE *ifile;
   int ok;

   if ((ifile=fopen(fname,"r")) == NULL)
      return bdd_error(BDD_FILE);

   ok = bdd_load(ifile, root);
   fclose(ifile);
   return ok;
}


int bdd_load(FILE *ifile, BDD *root)
{
   int n, vnum, tmproot, type; // TODO store type somewhere better
   char line[1000] = "";
   char name[20]   = ""; // TODO store somewhere better
   char *token;
   bool started;
   char *convCheck; // char that will be leftover after converting, can signal error
   while(!orderR)
   {
      fgets(line, sizeof(line), ifile);
      if(line[0] == '@')
      {
         started = true;
         typeR   = true;
         token = strtok(line, " ");
         if(!strcmp(token, "@BDD"))
         {
            type = 1; // TODO add enum/define of types
            continue;
         }
         else
         {
            type = 0; // unknown
            continue;
         }
      }
      if(started)
      {
         switch(line[0])
         {
            case '#':   // skip comment
               continue;
            case '%':
               token = strtok(line," ");
               if(!strcmp(token, "%Name"))
               {
                  nameR = true;

                  token = strtok(NULL," ");
                  if(token == NULL)
                     return bdd_error(BDD_FORMAT);

                  strncpy(name, token, sizeof(name) - 1);
                  name[sizeof(name) - 1] = '\0';
                  continue;
               }
               else if(!strcmp(token, "%Vars"))
               {
                  varsR = true;

                  token = strtok(NULL," ");
                  if(token == NULL)
                     return bdd_error(BDD_FORMAT);

                  vnum  = strtol(token, &convCheck, 10);
                  if(*convCheck != '\0' && *convCheck != '\n')
                  {
                     return bdd_error(BDD_FORMAT);
                  }
                  continue;
               }
               else if(!strcmp(token, "%Nodes"))
               {
                  nodesR = true;

                  token = strtok(NULL," ");
                  if(token == NULL)
                     return bdd_error(BDD_FORMAT);

                  lh_nodenum  = strtol(token, &convCheck, 10);
                  if(*convCheck != '\0' && *convCheck != '\n')
                  {
                     return bdd_error(BDD_FORMAT);
                  }
                  continue;
               }
               else if(!strcmp(token, "%Root"))
               {
                  rootR = true;

                  token = strtok(NULL," ");
                  if(token == NULL)
                     return bdd_error(BDD_FORMAT);

                  *root       = strtol(token, &convCheck, 10);
                  if(*convCheck != '\0' && *convCheck != '\n')
                  {
                     return bdd_error(BDD_FORMAT);
                  }

                  if(*root < 2) // check whether constant
                  {
                    orderR; // to account for missing ordering
                  }
                  continue;
               }
               else if(!strcmp(token, "%Ordering"))
               {
                  orderR = true;

                  if ((loadvar2level=(int*)malloc(sizeof(int)*vnum)) == NULL)
                     return bdd_error(BDD_MEMORY);

                  for (n=0 ; n<vnum ; n++) 
                  {
                     token = strtok(NULL, " ");
                     if(token == NULL)
                        return bdd_error(BDD_FORMAT);

                     loadvar2level[n] = strtol(token, &convCheck, 10);
                     if (*convCheck != '\0' && *convCheck != '\n')
                        return bdd_error(BDD_FORMAT);
                  }
                  continue;
               }
               else
               {
                  return bdd_error(BDD_FORMAT);
               }
               break;
            default:
               break;
         }
      }
   }

   bool allRead = typeR && nameR && rootR && varsR && orderR; // node number is not necessary
   if(!allRead)
   {
      return bdd_error(BDD_FORMAT);
   }


   if (*root == 0 || *root == 1) // check whether constant
   {
         return 0;
   }

   if (vnum > bddvarnum)
      bdd_setvarnum(vnum);

   if ((lh_table=(LoadHash*)malloc(lh_nodenum*sizeof(LoadHash))) == NULL)
      return bdd_error(BDD_MEMORY);

   for (n=0 ; n<lh_nodenum ; n++)
   {
      lh_table[n].first = -1;
      lh_table[n].next = n+1;
   }
   lh_table[lh_nodenum-1].next = -1;
   lh_freepos = 0;

   tmproot = bdd_loaddata(ifile);

   for (n=0 ; n<lh_nodenum ; n++)
      bdd_delref(lh_table[n].data);

   free(lh_table);
   free(loadvar2level);
   *root = 0;
   if (tmproot < 0)
      return tmproot;
   else
      *root = tmproot;

   return 0;
}

static int unresolved_realloc()
{
   if(un_nodes == NULL)
   {
      un_nodes = malloc(unresolved_size*sizeof(unresolved_node));
      if(un_nodes == NULL)
         return bdd_error(BDD_MEMORY);
      return 0;
   }

   if(unresolved < unresolved_size*lh_realloc_factor)
   {
      return 0;
   }

   unresolved_size = unresolved_size*2;
   unresolved_node *newTable = (unresolved_node*)realloc(un_nodes, unresolved_size*sizeof(unresolved_node));
   if (newTable == NULL)
      return bdd_error(BDD_MEMORY);
   un_nodes = newTable;
   
   return 0;
}

static void add_unresolved(int key, int var, int high, int low)
{
   unresolved_realloc();
   unresolved_node toadd = {.high = high, .low = low, .key = key, .var = var};
   un_nodes[unresolved] = toadd;
   unresolved++;
   return;
}

static void resolve_nodes(int *root)
{
   for(int i = 0; i<unresolved; i++)
   {
      unresolved_node toresolve = un_nodes[i];
      int lowH  = toresolve.low;
      int highH = toresolve.high;
      if (toresolve.low >= 2)
	      lowH = loadhash_get(toresolve.low);
      if (toresolve.high >= 2)
	      highH = loadhash_get(toresolve.high);

      if ( highH<0 || lowH<0 ){continue;} // check if it is possible to resolve
      else 
      {
         *root = bdd_addref( bdd_ite(bdd_ithvar(toresolve.var), highH, lowH) );

         loadhash_add(toresolve.key, *root);

         if(i == unresolved-1){unresolved--;} // if we processed last element, just decrement the num of unresolved
         else
         {
            un_nodes[i] = un_nodes[unresolved--]; // else, put last element to the place of processed
            i--; // decrement i to include the moved element         
         }

      }
   }
}

static int bdd_loaddata(FILE *ifile)
{
   int key,var,low,high,root=0,n;
   char line[1000] = "";

   while (fgets(line, sizeof(line), ifile) != NULL)
   {
      if(line[0] == '#')
      {
         n--;
         continue;
      }
      if (sscanf(line,"%d[%d] %d %d", &key, &var, &low, &high) != 4)
	      return bdd_error(BDD_FORMAT);
   int lowH  = low;
   int highH = high;
      if (low >= 2)
	 lowH = loadhash_get(low);
      if (high >= 2)
	 highH = loadhash_get(high);

      if ( var<0 )
         return bdd_error(BDD_FORMAT);
      if ( highH<0 || lowH<0 )
      {
         add_unresolved(key, var, high, low);
         printf("WARNING: Node %d has references to previously undefined nodes. Resolving. Performance will be heavily impacted.\n", key);
      }
      else
      {
         root = bdd_addref( bdd_ite(bdd_ithvar(var), highH, lowH) );

         loadhash_add(key, root);

         resolve_nodes(&root);
      }
   }

   for(int i = 0; i<unresolved; i++)
   {
      resolve_nodes(&root);
   }

   if(unresolved != 0){return bdd_error(BDD_FORMAT);}

   return root;
}


static void loadhash_add(int key, int data)
{
   if(lh_count >= lh_nodenum*LH_INIT_REALLOC_FACTOR)
   {
      loadhash_realloc();
   }

   int hash = key % lh_nodenum;
   int pos = lh_freepos;

   lh_freepos = lh_table[pos].next;
   lh_table[pos].next = lh_table[hash].first;
   lh_table[hash].first = pos;

   lh_table[pos].key = key;
   lh_table[pos].data = data;
   lh_count++;
}


static int loadhash_get(int key)
{
   int hash = lh_table[key % lh_nodenum].first;

   while (hash != -1  &&  lh_table[hash].key != key)
      hash = lh_table[hash].next;

   if (hash == -1)
      return -1;
   return lh_table[hash].data;
}


static int loadhash_realloc()
{
   int oldSize        = lh_nodenum;
   LoadHash *oldTable = lh_table;

   lh_nodenum         = lh_nodenum * 2;

   if ((lh_table=(LoadHash*)malloc(lh_nodenum*sizeof(LoadHash))) == NULL)
      return bdd_error(BDD_MEMORY);

   for (int n=0 ; n<lh_nodenum ; n++)
   {
      lh_table[n].first = -1;
      lh_table[n].next = n+1;
   }
   lh_table[lh_nodenum-1].next = -1;
   lh_freepos = 0;

   for(int n = 0; n < oldSize; n++)
   {
      if(oldTable[n].key != 0)
      {
         loadhash_add(oldTable[n].key, oldTable[n].data);
      }
   }

   free(oldTable);
   return 0;
}



/* EOF */
