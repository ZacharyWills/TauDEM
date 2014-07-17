/*  TauDEM D8FlowDir function to compute flow direction based on d8 flow model.
     
  David G Tarboton, Dan Watson, Jeremy Neff
  Utah State University     
  May 23, 2010
  
*/

/*  Copyright (C) 2010  David Tarboton, Utah State University

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License 
version 2, 1991 as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

A copy of the full GNU General Public License is included in file 
gpl.html. This is also available at:
http://www.gnu.org/copyleft/gpl.html
or from:
The Free Software Foundation, Inc., 59 Temple Place - Suite 330, 
Boston, MA  02111-1307, USA.

If you wish to use or incorporate this program (or parts of it) into 
other software that does not meet the GNU General Public License 
conditions contact the author to request permission.
David G. Tarboton  
Utah State University 
8200 Old Main Hill 
Logan, UT 84322-8200 
USA 
http://www.engineering.usu.edu/dtarb/ 
email:  dtarb@usu.edu 
*/

//  This software is distributed from http://hydrology.usu.edu/taudem/
#include "d8.h"
#include <mpi.h>
#include "linearpart.h"
#include "createpart.h"
#include "commonLib.h"
#include "tiffIO.h"
#include "Node.h"

using namespace std;

double fact[9];

//Checks if cells cross
int dontCross(int k, int i, int j, linearpart<short>& flowDir)
{
	long in1,jn1,in2,jn2;

    int n1, c1, n2, c2;

    switch(k){
        case 2:
            n1=1; c1=4; n2=3; c2=8;
            break;
        case 4:
            n1=3; c1=6; n2=5; c2=2;
            break;
        case 6:
            n1=7; c1=4; n2=5; c2=8;
            break;
        case 8:
            n1=1; c1=6; n2=7; c2=2;
            break;
        default:
            return 0;
    }

    in1=i+d1[n1];
    jn1=j+d2[n1];
    in2=i+d1[n2];
    jn2=j+d2[n2];

    if( (flowDir.getData(in1,jn1)) == c1 || (flowDir.getData(in2,jn2)) == c2)
        return 1;

    return 0;
}

//Set positive flowdirections of elevDEM
void setFlow(int i, int j, linearpart<short>& flowDir, linearpart<float>& elevDEM, linearpart<long>& area, int useflowfile)
{
    float slope,smax=0;
    long in,jn;
    short k,dirnb;
    short tempShort;
    int aneigh = -1;
    int amax=0;

    for(k=1; k<=8 && !flowDir.isNodata(i,j); k+=2)
    {
        in=i+d1[k];
        jn=j+d2[k];

        slope = fact[k] * (elevDEM.getData(i,j) - elevDEM.getData(in,jn));

        if(useflowfile == 1) {	
            aneigh = area.getData(in,jn);
        }

        if (aneigh > amax && slope >= 0) {
            amax = aneigh;
            dirnb = flowDir.getData(in,jn,tempShort);
            if(dirnb > 0 && abs(dirnb - k) != 4){
                flowDir.setData(i, j, k);
            }
        }
        if (slope > smax) {
            smax=slope;
            dirnb=flowDir.getData(in,jn);

            if(dirnb >0 && abs(dirnb-k) == 4)
                flowDir.setToNodata(i,j);
            else
                flowDir.setData(i,j,k);
        }
    }

    for(k=2;k<=8 && !flowDir.isNodata(i,j); k+=2)
    {
        in=i+d1[k];
        jn=j+d2[k];

        slope = fact[k] * (elevDEM.getData(i,j) - elevDEM.getData(in,jn));

        if (slope > smax && dontCross(k,i,j,flowDir)==0){
            smax = slope;
            dirnb = flowDir.getData(in,jn);

            if (dirnb >0 && abs(dirnb-k) == 4)
                flowDir.setToNodata(i,j);
            else
                flowDir.setData(i,j,k);
        }
    }
}

//Calculate the slope information of flowDir to slope
void calcSlope(linearpart<short>& flowDir, linearpart<float>& elevDEM, linearpart<float>& slope) {
	float elevDiff;

	int nx = elevDEM.getnx();
	int ny = elevDEM.getny();

	for(int j = 0; j < ny; j++) {
		for(int i=0; i < nx; i++) {
			//If i,j is on the border or flowDir has no data, set slope(i,j) to slopeNoData 
			if ( flowDir.isNodata(i,j) || !flowDir.hasAccess(i-1,j) || !flowDir.hasAccess(i+1,j) || 
					!flowDir.hasAccess(i,j-1) || !flowDir.hasAccess(i,j+1) )  {
				slope.setData(i,j,-1.0f);
			}
			else {
			    short flowDirection = flowDir.getData(i,j);

				int in = i + d1[flowDirection];	
				int jn = j + d2[flowDirection];	

				elevDiff = elevDEM.getData(i,j) - elevDEM.getData(in,jn);
				slope.setData(i,j, float(elevDiff*fact[flowDirection]));
			}
		}
	}
}


//Open files, Initialize grid memory, makes function calls to set flowDir, slope, and resolvflats, writes files
int setdird8( char* demfile, char* pointfile, char *slopefile, char *flowfile, int useflowfile, int prow, int pcol)
{
	MPI_Init(NULL,NULL);

	//Only needed to output time
	int rank,size;
	MPI_Comm_rank(MCW,&rank);
	MPI_Comm_size(MCW,&size);
	if(rank==0)
	{
		printf("D8FlowDir version %s\n",TDVERSION);
		fflush(stdout);
	}

	double begint = MPI_Wtime();

	//Read DEM from file
	tiffIO dem(demfile, FLOAT_TYPE);
	long totalX = dem.getTotalX();
	long totalY = dem.getTotalY();
	double dx = dem.getdx();
	double dy = dem.getdy();
	
	linearpart<float> elevDEM;//CreateNewPartition(dem.getDatatype(), totalX, totalY, dx, dy, *(float*)dem.getNodata());
    elevDEM.init(totalX, totalY, dx, dy, MPI_FLOAT, *(float*) dem.getNodata());

	int xstart, ystart;
	int nx = elevDEM.getnx();
	int ny = elevDEM.getny();
	elevDEM.localToGlobal(0, 0, xstart, ystart);

	double headert = MPI_Wtime();

	if(rank==0)
	{
		float timeestimate=(2.8e-9*pow((double)(totalX*totalY),1.55)/pow((double) size,0.65))/60+1;  // Time estimate in minutes
		//fprintf(stderr,"%d %d %d\n",totalX,totalY,size);
		fprintf(stderr,"This run may take on the order of %.0f minutes to complete.\n",timeestimate);
		fprintf(stderr,"This estimate is very approximate. \nRun time is highly uncertain as it depends on the complexity of the input data \nand speed and memory of the computer. This estimate is based on our testing on \na dual quad core Dell Xeon E5405 2.0GHz PC with 16GB RAM.\n");
		fflush(stderr);
	}

	dem.read(xstart, ystart, ny, nx, elevDEM.getGridPointer());
	elevDEM.share();

	double readt = MPI_Wtime();
	
	//Creates empty partition to store new flow direction 
	short flowDirNodata = MISSINGSHORT;
	linearpart<short> flowDir;//CreateNewPartition(SHORT_TYPE, totalX, totalY, dx, dy, flowDirNodata);
	flowDir.init(totalX, totalY, dx, dy, MPI_SHORT, flowDirNodata);

//	flowDir = new linearpart<short>;
//	flowDir->init(totalX, totalY, dx, dy, MPI_SHORT, short(-32768));

	//If using flowfile is enabled, read it in
	
	linearpart<long> area;
	area.init(totalX, totalY, dx, dy, MPI_LONG, -1);

	if(useflowfile == 1) {
		tiffIO flow(flowfile,SHORT_TYPE);

		linearpart<short> imposedflow;
        imposedflow.init(flow.getTotalX(), flow.getTotalY(), flow.getdx(), flow.getdy(), MPI_SHORT, *(short*) flow.getNodata());

		if(!dem.compareTiff(flow)) {
			printf("Error using imposed flow file.\n");	
			return 1;
		}
		
		for(int j=0; j < elevDEM.getny(); j++) {
			for(int i=0; i < elevDEM.getnx(); i++ ) {
				short data;
				imposedflow.getData(i,j,data);
				if (imposedflow.isNodata(i,j) || !imposedflow.hasAccess(i-1,j) || !imposedflow.hasAccess(i+1,j) ||
				    !imposedflow.hasAccess(i,j-1) || !imposedflow.hasAccess(i,j+1)){
					//Do nothing
				}
				else if((data>0) && data <=8 ) 
						flowDir.setData(i,j,data);
			}
		}
		//TODO - why is this here?
		//darea( &flowDir, &area, NULL, NULL, 0, 1, NULL, 0, 0 );
	}

	long numFlat, totalNumFlat, lastNumFlat;

	double computeSlopet;
	{
		//Creates empty partition to store new slopes
		float slopeNodata = -1.0f;

		linearpart<float> slope;
        slope.init(totalX, totalY, dx, dy, MPI_FLOAT, slopeNodata);
		
		numFlat = setPosDir(elevDEM, flowDir, area, useflowfile);
		calcSlope(flowDir, elevDEM, slope);

		//Stop timer
		computeSlopet = MPI_Wtime();
		char prefix[6]="sd8";

		tiffIO slopeIO(slopefile, FLOAT_TYPE, &slopeNodata, dem);
		slopeIO.write(xstart, ystart, ny, nx, slope.getGridPointer(), prefix, prow, pcol);
	}  // This bracket intended to destruct slope partition and release memory

	double writeSlopet = MPI_Wtime();

	MPI_Allreduce(&numFlat,&totalNumFlat,1,MPI_LONG,MPI_SUM,MCW);
	if(rank==0)
	{
		fprintf(stderr,"All slopes evaluated. %ld flats to resolve.\n",totalNumFlat);
		fflush(stderr);
	}

	queue<node> que;  //  que to be used in resolveflats
	bool first=true;  //  Variable to be used in iteration to know whether first or subsequent iteration
	if(totalNumFlat > 0)
	{
		lastNumFlat=totalNumFlat;
		totalNumFlat = resolveflats(elevDEM, flowDir, &que, first);  
		//Repeatedly call resolve flats until there is no change 
		while(totalNumFlat > 0  && totalNumFlat < lastNumFlat)
		{
			if(rank==0)
			{
				fprintf(stderr,"Iteration complete. Number of flats remaining: %ld\n",totalNumFlat);
				fflush(stderr);
			}
			lastNumFlat=totalNumFlat;
			totalNumFlat = resolveflats(elevDEM, flowDir, &que, first);
		}
	}

	//Timing info
	double computeFlatt = MPI_Wtime();
	char prefix[6] = "p";
	tiffIO pointIO(pointfile, SHORT_TYPE, &flowDirNodata, dem);
	pointIO.write(xstart, ystart, ny, nx, flowDir.getGridPointer(),prefix,prow,pcol);
	double writet = MPI_Wtime();

    double headerRead, dataRead, computeSlope, writeSlope, computeFlat,writeFlat, total,temp;
    headerRead = headert-begint;
    dataRead = readt-headert;
    computeSlope = computeSlopet-readt;
    writeSlope = writeSlopet-computeSlopet;
    computeFlat = computeFlatt-writeSlopet;
    writeFlat = writet-computeFlatt;
    total = writet - begint;

    MPI_Allreduce (&headerRead, &temp, 1, MPI_DOUBLE, MPI_SUM, MCW);
    headerRead = temp/size;
    MPI_Allreduce (&dataRead, &temp, 1, MPI_DOUBLE, MPI_SUM, MCW);
    dataRead = temp/size;
    MPI_Allreduce (&computeSlope, &temp, 1, MPI_DOUBLE, MPI_SUM, MCW);
    computeSlope = temp/size;
    MPI_Allreduce (&computeFlat, &temp, 1, MPI_DOUBLE, MPI_SUM, MCW);
    computeFlat = temp/size;
    MPI_Allreduce (&writeSlope, &temp, 1, MPI_DOUBLE, MPI_SUM, MCW);
    writeSlope = temp/size;
    MPI_Allreduce (&writeFlat, &temp, 1, MPI_DOUBLE, MPI_SUM, MCW);
    writeFlat = temp/size;
    MPI_Allreduce (&total, &temp, 1, MPI_DOUBLE, MPI_SUM, MCW);
    total = temp/size;

	if(rank == 0)
    {
		printf("Processors: %d\nHeader read time: %f\nData read time: %f\nCompute Slope time: %f\nWrite Slope time: %f\nResolve Flat time: %f\nWrite Flat time: %f\nTotal time: %f\n",
		  size,headerRead,dataRead, computeSlope, writeSlope,computeFlat,writeFlat,total);
	}
	
	MPI_Finalize();
	return 0;
}

// Sets only flowDir only where there is a positive slope
// Returns number of cells which are flat
long setPosDir(linearpart<float>& elevDEM, linearpart<short>& flowDir, linearpart<long>& area, int useflowfile)
{
    double dx = elevDEM.getdx();
    double dy = elevDEM.getdy();
    long nx = elevDEM.getnx();
    long ny = elevDEM.getny();
    long numFlat = 0;

    //Set direction factors
    for(int k=1; k<= 8; k++){
        fact[k] = (double) (1./sqrt(d1[k]*dx*d1[k]*dx + d2[k]*d2[k]*dy*dy));
    }

    for(int j = 0; j < ny; j++) {
        for(int i=0; i < nx; i++ ) {
            //FlowDir is nodata if it is on the border OR elevDEM has no data
            if (elevDEM.isNodata(i,j) || !elevDEM.hasAccess(i-1,j) || !elevDEM.hasAccess(i+1,j) || 
                    !elevDEM.hasAccess(i,j-1) || !elevDEM.hasAccess(i,j+1)) {
                //do nothing
                continue;
            }

            //Check if cell is "contaminated" (neighbors have no data)
            //  set flowDir to noData if contaminated
            int con = 0;
            for(int k=1;k<=8 && con != -1;k++) {
                int in=i+d1[k];
                int jn=j+d2[k];

                if(elevDEM.isNodata(in,jn))
                    con=-1;
            }

            if (con == -1) {
                flowDir.setToNodata(i,j);
            } else {
                // If cell is not contaminated,
                flowDir.setData(i,j,0);
                setFlow(i,j, flowDir, elevDEM, area, useflowfile);

                if (flowDir.getData(i,j) == 0)
                    numFlat++;
            }

        }
    }

    return numFlat;
}

//  Function to set flow direction based on incremented artificial elevations
void setFlow2(int i,int j, tdpartition *flowDir, tdpartition *elevDEM, tdpartition *elev2, tdpartition *dn) 
{
/*  This function sets directions based upon secondary elevations for
  assignment of flow directions across flats according to Garbrecht and Martz
  scheme.  There are two possibilities: 
	A.  The neighbor is outside the flat set
	B.  The neighbor is in the flat set.
	In the case of A the input elevations are used and if a draining neighbor is found it is selected.  
	Case B requires slope to be positive.  Remaining flats are removed by iterating this process*/

	float slope,smax,ed;
	long  in,jn;
	short k;
	smax=0.;
	short tempShort;
	float tempFloat;
	short order[8]={1,3,5,7,2,4,6,8};
	for(int ii=0;ii<8;ii++)  //k=1; k<=8; k=k+1)
	{
		k=order[ii];
		jn=j+d2[k];  //y
		in=i+d1[k];  //x
		dn->getData(in,jn,tempShort);
		if(tempShort > 0)  // In flat
		{
			slope = fact[k]*(elev2->getData(i,j,tempShort)-elev2->getData(in,jn,tempShort));
			if(slope > smax)
			{
				flowDir->setData(i,j,k);
				smax=slope;
			}
		}
		else   // neighbor is not in flat
		{
			ed=elevDEM->getData(i,j,tempFloat)-elevDEM->getData(in,jn,tempFloat);
			if(ed >= 0)
			{
				flowDir->setData(i,j,k);
				break;  // Found a way out - this is outlet
			}
		}
	}
}

//************************************************************************

//Resolve flat cells according to Garbrecht and Martz
long resolveflats(linearpart<float>& elevDEM, linearpart<short>& flowDir, queue<node> *que, bool &first)
{
	elevDEM.share();
	flowDir.share();
	//Header data
	long totalx = elevDEM.gettotalx();
	long totaly = elevDEM.gettotaly();
	long nx = elevDEM.getnx();
	long ny = elevDEM.getny();
	double dx = elevDEM.getdx();
	double dy = elevDEM.getdy();

	int rank;
	MPI_Comm_rank(MCW,&rank);

	long i,j,k,in,jn;
	bool doNothing, done;
	short tempShort;
	long numInc, numIncOld, numIncTotal;

	//create and initialize temporary storage for Garbrecht and Martz
	//elev2 = CreateNewPartition(SHORT_TYPE, totalx, totaly, dx, dy, 1);
	
	linearpart<short> elev2;// = new linearpart<short>;
	elev2.init(totalx, totaly, dx, dy, MPI_SHORT, 1);
	
	//  The assumption here is that resolving a flat does not increment a cell value 
	//  more than fits in a short
	linearpart<short> dn;// CreateNewPartition(SHORT_TYPE, totalx, totaly, dx, dy, 0);
	dn.init(totalx, totaly, dx, dy, MPI_SHORT, 0);

	node temp;
	long nflat=0, iflat;
	//  First time through add flat grid cells indicated by flowdir = 0 on to queue
	//  The queue is retained for later passes so this only needs be done at beginning
	if(first)
	{
		first=false;
		for(j=0; j<ny; j++){
			for(i=0; i<nx; i++){
				if(flowDir.getData(i,j)==0)
				{
					temp.x=i; temp.y=j; que->push(temp); nflat++;
				}
			}
		}
	}
	else {
 		nflat=que->size();
    }

	dn.share();
	elev2.share();

	//incfall - drain toward lower ground
	// Setting up while loop to calculate elev2 - the grid draining to lower ground
	done = false;
	numIncOld = -1; //holds number of grid cells incremented on previous iteration 
	//  use -1 to force inequality on first pass through
	short st = 1; // used to indicate the level to which elev2 has been incremented 
	float elevDiff;
	numIncTotal = 0; 
	if(rank==0)
	{
		fprintf(stderr,"Draining flats towards lower adjacent terrain\n");  
		fflush(stderr);
	}

	while(numIncTotal != numIncOld){  // Continue to loop as long as grid cells are being incremented
		numInc = 0;
		numIncOld = numIncTotal;
		for(iflat=0; iflat < nflat; iflat++)
		{
			temp=que->front(); que->pop(); i=temp.x; j=temp.y; que->push(temp);

				doNothing=false;

				float elev = elevDEM.getData(i, j);

				for(k=1; k<=8; k++){
					if(dontCross(k,i,j, flowDir)==0){
						jn = j + d2[k];
						in = i + d1[k];
						elevDiff = elev - elevDEM.getData(in,jn);
						tempShort=flowDir.getData(in,jn); 
						if(elevDiff >= 0 && tempShort > 0 && tempShort < 9) //adjacent cell drains and is equal or lower in elevation so this is a low boundary
								doNothing = true;
						else if(elevDiff == 0) //if neighbor is in flat
							if(elev2.getData(in,jn) >= 0 && elev2.getData(in,jn)<st) //neighbor is not being incremented
								doNothing = true;
					}
				}
				if(!doNothing){
					elev2.addToData(i, j, 1);
					numInc++;
				}
		}
		elev2.share();
		MPI_Allreduce(&numInc, &numIncTotal, 1, MPI_LONG, MPI_SUM, MCW);
		//  only break from while when total from all processes is no longer converging
		st++;
		if(rank==0)
		{
			fprintf(stderr,".");  // print a . at each pass to give an indication of progress
			fflush(stderr);
		}
	}

	if(numIncTotal > 0)  // Not all grid cells were resolved - pits remain
		// Remaining grid cells are unresolvable pits
	{
		//There are pits remaining - set direction to no data
		for(iflat=0; iflat < nflat; iflat++)
		{
			temp=que->front(); que->pop(); i=temp.x; j=temp.y; que->push(temp);
			doNothing=false;
				for(k=1; k<=8; k++){
					if(dontCross(k,i,j, flowDir)==0){
						jn = j + d2[k];
						in = i + d1[k];
						elevDiff = elevDEM.getData(i,j) - elevDEM.getData(in,jn);
						tempShort=flowDir.getData(in,jn); 
						if(elevDiff >= 0 && tempShort > 0 && tempShort < 9) //adjacent cell drains and is equal or lower in elevation so this is a low boundary
								doNothing = true;
						else if(elevDiff == 0) //if neighbor is in flat
							if(elev2.getData(in,jn) >=0 && elev2.getData(in,jn)<st) //neighbor is not being incremented
								doNothing = true;
					}
				}
				if(!doNothing)
					flowDir.setToNodata(i,j);  // mark pit
					//flowDir->setData(i,j,short(9));  // mark pit
		}
		flowDir.share();
		
		//
		//long total = 0;
		//MPI_Allreduce(&allDone, &total, 1, MPI_LONG, MPI_SUM, MCW);
		//if(total != 0)
		//	done = false;
		//if(numIncOld == 0)
		//	done = true;
	}
	//  DGT moved from above - write directly into elev2
	//s = CreateNewPartition(SHORT_TYPE, totalx, totaly, dx, dy, 0);  //  Use 0 as no data to avoid need to initialize
	linearpart<short> s;
	s.init(totalx, totaly, dx, dy, MPI_SHORT, 0);

	//incrise - drain away from higher ground
	done = false;
	numIncOld = 0;
	if(rank==0)
	{
		fprintf(stderr,"\nDraining flats away from higher adjacent terrain\n");  
		fflush(stderr);
	}

	while(!done){
		numInc = 0;
		for(iflat=0; iflat < nflat; iflat++)
		{
				temp=que->front(); que->pop(); i=temp.x; j=temp.y; que->push(temp);

				for(k=1; k<=8; k++)
				{
					jn = j + d2[k];
					in = i + d1[k];

					if(elevDEM.getData(i,j) - elevDEM.getData(in,jn)<0)	//adjacent cell is higher
						dn.setData(i,j,short(1));

					if(dn.getData(in,jn)>0 && s.getData(in,jn)>0) //adjacent cell has been marked already
						dn.setData(i,j,short(1));
				}
		} 
		dn.share();

		for(j=0; j<ny; j++){
			for(i=0; i<nx; i++){
				tempShort = dn.getData(i,j);

				s.addToData(i,j,(tempShort>0 ? short(1) : short(0)));

				if(tempShort>0)
				    numInc++;
			}
		}

		s.share();
		dn.share();
		MPI_Allreduce(&numInc, &numIncTotal, 1, MPI_LONG, MPI_SUM, MCW);
		if(numIncTotal==numIncOld) done=true;
		numIncOld = numIncTotal;
		if(rank==0)
		{
			fprintf(stderr,".");  // print a . at each pass to give an indication of progress
			fflush(stderr);
		}
    }

    for(iflat=0; iflat < nflat; iflat++)
    {
        temp=que->front(); que->pop(); i=temp.x; j=temp.y; que->push(temp);

        elev2.addToData(i,j, s.getData(i,j));
    }

	elev2.share();

	long localStillFlat = 0;
	long totalStillFlat = 0;

	if(rank==0)
	{
		fprintf(stderr,"\nSetting directions\n");  
		fflush(stderr);
	}

    for(iflat=0; iflat < nflat; iflat++)
    {
        temp=que->front(); que->pop(); i=temp.x; j=temp.y; //  Do not push que on this last one - so que is empty at end

        setFlow2(i, j, &flowDir, &elevDEM, &elev2, &dn);

        if(flowDir.getData(i,j) == 0)
        {
            que->push(temp);
            localStillFlat++;
        }
    }

	MPI_Allreduce(&localStillFlat, &totalStillFlat, 1, MPI_LONG, MPI_SUM, MCW);
	if(totalStillFlat > 0)  //  We will have to iterate again so overwrite original elevation with the modified ones and hope for the best
	{
		for(j=0; j<ny; j++)
			for(i=0; i<nx; i++){
				elevDEM.setData(i,j,(float) elev2.getData(i,j));//set/add change jjn friday
			}
	}
	
	return totalStillFlat;
}
