// This software has been written by Christine Solnon.
// It is distributed under the CeCILL-B FREE SOFTWARE LICENSE
// see http://www.cecill.info/licences/Licence_CeCILL-B_V1-en.html for more details

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/resource.h>

// define boolean type as char
#define true 1
#define false 0
#define bool char

#include "graph.c"
#include "domains.c"
#include "allDiff.c"
#include "lad.c"

// Global variables
int nbNodes = 1;      // number of nodes in the search tree
int nbFail = 0;       // number of failed nodes in the search tree
int nbSol = 0;        // number of solutions found
struct rusage ru;     // reusable structure to get CPU time usage

bool filter(bool induced, Tdomain* D, Tgraph* Gp, Tgraph* Gt){
	// filter domains of all vertices in D->toFilter wrt LAD and ensure GAC(allDiff)
	// return false if some domain becomes empty; true otherwise
	int u, v, i, oldNbVal;
	while (!toFilterEmpty(D)){
		while (!toFilterEmpty(D)){
			u=nextToFilter(D,Gp->nbVertices); 
			oldNbVal = D->nbVal[u];
			i=D->firstVal[u]; 
			while (i<D->firstVal[u]+D->nbVal[u]){
				// for every target node v in D(u), check if G_(u,v) has a covering matching
				v=D->val[i]; 
				if (checkLAD(u,v,D,Gp,Gt)) i++;
				else if (!removeValue(u,v,D,Gp,Gt)) return false;
			}
			if ((D->nbVal[u]==1) && (oldNbVal>1) && (!matchVertex(u,induced,D,Gp,Gt))) return false;
			if (D->nbVal[u]==0) return false;
		}
		if (!ensureGACallDiff(induced,Gp,Gt,D)) return false;
	}
	return true;
}



bool solve(int timeLimit, bool firstSol, bool induced, int verbose, Tdomain* D, Tgraph* Gp, Tgraph* Gt){
	// if firstSol then search for the first solution; otherwise search for all solutions
	// if induced then search for induced subgraphs; otherwise search for partial subgraphs
	// return false if CPU time limit exceeded before the search is completed
	// return true otherwise
	
	int u, v, minDom, i; 
	int nbVal[Gp->nbVertices];
	int globalMatching[Gp->nbVertices];
	
	nbNodes++;

	getrusage(RUSAGE_SELF, &ru);
	if (ru.ru_utime.tv_sec >= timeLimit)	
		// CPU time limit exceeded
		return false;
	
	if (!filter(induced,D,Gp,Gt)){ 
		// filtering has detected an inconsistency
		if (verbose == 2) printf("Filtering has detected an inconsistency\n");
		nbFail++;
		resetToFilter(D,Gp->nbVertices);
		return true;
	}	
	
	// The current node of the search tree is consistent wrt to LAD and GAC(allDiff)
	// Save domain sizes and global all different matching
	// and search for the non matched vertex minDom with smallest domain
	minDom=-1;
	for (u=0; u<Gp->nbVertices; u++){
		nbVal[u]=D->nbVal[u];
		if ((nbVal[u]>1) && ((minDom<0) || (nbVal[u]<nbVal[minDom]))) minDom=u;
		globalMatching[u] = D->globalMatchingP[u];
	}
	
	if (minDom==-1){ 
		// All vertices are matched => Solution found
		nbSol++;
		if (verbose >= 1){
			printf("Solution %d: ",nbSol);
			for (u=0; u<Gp->nbVertices; u++) printf("%d=%d ",u,D->val[D->firstVal[u]]);
			printf("\n");
		}
		resetToFilter(D,Gp->nbVertices);
		return true;
	}
	
	// save the domain of minDom to iterate on its values
	int val[D->nbVal[minDom]];
	for (i=0; i<D->nbVal[minDom]; i++) val[i]=D->val[D->firstVal[minDom]+i];
	
	// branch on minDom=v, for every target node v in D(u)
	for(i=0; ((i<nbVal[minDom]) && ((firstSol==0)||(nbSol==0))); i++){
		v = val[i]; 
		if (verbose == 2) printf("Branch on %d=%d\n",minDom,v);
		if ((!removeAllValuesButOne(minDom,v,D,Gp,Gt)) || (!matchVertex(minDom,induced,D,Gp,Gt))){
			if (verbose == 2) printf("Inconsistency detected while matching %d to %d\n",minDom,v);
			nbFail++; 
			nbNodes++;
			resetToFilter(D,Gp->nbVertices);
		} 
		else if (!solve(timeLimit,firstSol,induced,verbose,D,Gp,Gt))
			// CPU time exceeded
			return false;
		// restore domain sizes and global all different matching
		if (verbose == 2) printf("End of branch %d=%d\n",minDom,v);
		memset(D->globalMatchingT,-1,sizeof(int)*Gt->nbVertices);
		for (u=0; u<Gp->nbVertices; u++){
			D->nbVal[u] = nbVal[u];
			D->globalMatchingP[u] = globalMatching[u];
			D->globalMatchingT[globalMatching[u]] = u;
		}
	}
	return true;
}


void usage(int status){
	// print usage notice and exit with status code status
	printf("Usage:\n");
	printf("  -p FILE  Input pattern graph (mandatory)\n");
	printf("  -t FILE  Input target graph (mandatory)\n\n"); 
	printf("  -d FILE  Input domain\n"); 
	printf("  -l INT   Set CPU time limit in seconds (default: 60)\n");
	printf("  -f       Stop at first solution\n");
	printf("  -i       Search for an induced subgraph (default: partial subgraph)\n");
	printf("  -v       Print solutions (default: only number of solutions)\n");
	printf("  -vv      Be verbose\n");
	printf("  -h       Print this help message\n");
	exit(status);
}

void parse(int* timeLimit, bool* firstSol, bool* i, int* verbose, bool* initialDomains,  char* fileNameGp, char* fileNameGt, char* fileNameD, char* argv[], int argc){
	// get parameter values
	// return false if an error occurs; true otherwise
	char ch;
	extern char* optarg;
	while ( (ch = getopt(argc, argv, "hik:p:t:a:fl:vd:"))!=-1 ) {
		switch(ch) {
			case 'v': (*verbose)++; break;
			case 'f': *firstSol=true; break;
			case 'l': *timeLimit=atoi(optarg); break;
			case 'i': *i=true; break;
			case 'p': strncpy(fileNameGp, optarg, 254); break;
			case 't': strncpy(fileNameGt, optarg, 254); break;
			case 'd': *initialDomains=1; strncpy(fileNameD, optarg, 254); break;
			case 'h': usage(0);
			default:
				printf("Unknown option: %c.\n", ch);
				usage(2);
		}
	}
	if (fileNameGp[0] == 0){
		printf("Error: no pattern graph given.\n");
		usage(2);
	}
	if (fileNameGt[0] == 0){
		printf("Error: no target graph given.\n");
		return usage(2);
	}
}

int printStats(bool timeout){
	// print statistics line and return exit status depending on timeout
	getrusage(RUSAGE_SELF, &ru);
	if (timeout)
		printf("CPU time exceeded");
	else
		printf("Run completed");
	printf(": %d solutions; %d fail nodes; %d nodes; %d.%06d seconds\n",
		   nbSol, nbFail, nbNodes,
		   (int) ru.ru_utime.tv_sec, (int) ru.ru_utime.tv_usec);
	return timeout;
}

int main(int argc, char* argv[]){
	// Parameters
	char fileNameGp[1024]; // Name of the file that contains the pattern graph
	char fileNameGt[1024]; // Name of the file that contains the target graph
	char fileNameD[1024];  // Name of the file that contains initial domains
	int timeLimit=60;      // Default: CPU time limit set to 60 seconds
	int verbose = 0;       // Default: non verbose execution
	bool induced = false;  // Default: partial subgraph
	bool firstSol = false; // Default: search for all solutions
	bool initialDomains = false;// Default: initial domains are not provided
	fileNameGp[0] = 0;
	fileNameGt[0] = 0;
	parse(&timeLimit, &firstSol, &induced, &verbose, &initialDomains,
	      fileNameGp, fileNameGt, fileNameD, argv, argc);
	if (verbose >= 1){
		printf("Parameters: induced=%d firstSol=%d timeLimit=%d verbose=%d fileNameGp=%s fileNameGt=%s",
			   induced,firstSol,timeLimit,verbose,fileNameGp,fileNameGt);
		if (initialDomains>0) printf(" fileNameD=%s\n",fileNameD);
		else printf("\n");
	}
	
	// Initialize graphs
	Tgraph Gp;       // Pattern graph
	Tgraph Gt;       // Target graph
	createGraph(fileNameGp, &Gp);
	createGraph(fileNameGt, &Gt);
	if (Gp.nbVertices > Gt.nbVertices){
		nbFail++;
		return printStats(false);
	}
	
	// Initialize domains
	Tdomain D;       // Domains
	if (!initDomains(initialDomains, fileNameD, &D, &Gp, &Gt))
		return printStats(false);
	
	// Check the global all different constraint
	if ((!updateMatching(Gp.nbVertices,Gt.nbVertices,D.nbVal,D.firstVal,D.val,D.globalMatchingP)) ||
		(!ensureGACallDiff(induced,&Gp,&Gt,&D))){
		nbFail++;
		return printStats(false);
	}
	int u;
	for (u=0; u<Gp.nbVertices; u++)
		D.globalMatchingT[D.globalMatchingP[u]] = u;
	
	// Math all vertices with singleton domains
	int nbToMatch = 0;
	int toMatch[Gp.nbVertices];
	for (u=0; u<Gp.nbVertices; u++)
		if (D.nbVal[u] == 1) 
			toMatch[nbToMatch++] = u;
	if (!matchVertices(nbToMatch,toMatch,induced,&D,&Gp,&Gt)){
		nbFail++;
		return printStats(false);
	}
	
	
	// Solve
	return printStats(!solve(timeLimit,firstSol, induced, verbose, &D, &Gp, &Gt));
}
