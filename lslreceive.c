/*
 *  lslreceive.c
 *  lslreceive
 *
 *  Created by John Iversen on 1/1/14.
 *
 *  receive a list of strings from an lsl stream, use these to control max
 *
 *  Adapted from Grace Leslie's lslaudio.c
 *
 *   this is a bit more general purpose:
 *      -can specify the number of channels and stream name
 *          defaults: 10, 'MAX'
 *          usage: [lslreceive nchan streamname]
 *          thus, one can have multiple lsl streams being used for different purposes, rather than
 *          having to multiplex different messages
 *      -makes no assumptions about data types (all are returned as symbols.) After unpacking the list, 
 *          user can use [fromsymbol] to convert to numbers. You could use info's channel descriptions to
 *          customise this and name outputs, etc...
 *      -outputs the sender's timestamp, and the local timestamp, should these be of interest e.g. for latency calculations
 */

#include "ext.h"
#include "ext_obex.h"
#include "lsl_c.h"
#include <stdio.h>

#define DEFAULT_NCHAN 10        //pull this from object parameters
#define DEFAULT_STREAM_NAME "MAX"
#define MAX_NCHAN 2000          //some unreaonably large value
#define POLLING_INTERVAL_MS 1   //poll stream this often (Q: is there any way to specify a callback?)
#define MAX_STREAM_NAME_LENGTH 200


typedef struct _lslreceive
{
	t_object m_obj;
	void * m_outlet[3];
	void * m_clock;
    char* cursample[MAX_NCHAN];     /* array to hold our current sample string*/
    
	
    int lsl_nchan;              /* number of channels in the stream (speacified when creating object) */
    char lsl_stream_name[MAX_STREAM_NAME_LENGTH];  /* name of stream */
	lsl_streaminfo lsl_info;	/* the streaminfo returned by the resolve call */
	lsl_inlet lsl_inlet;		/* a stream inlet to get samples from */
	int lsl_errcode;			/* error code (lsl_lost_error or timeouts) */
    double lsl_timestamp;		/* time stamp of the current sample (in sender time) */
    double lsl_local_timestamp; /* time stamp of receipt in local time */

} t_lslreceive;

static t_class* s_lslreceive_class = NULL;

void* lslreceive_new(t_symbol* s, long argc, t_atom* argv);
void  lslreceive_free(t_lslreceive* x);
void  lslreceive_assist(t_lslreceive* x, void* b, long m, long a, char* s);
void  lslreceive_getSample(t_lslreceive *x);

int main(void)
{
	t_class* c;
    
    c = class_new("lslreceive", (method)lslreceive_new, (method)lslreceive_free,
                  sizeof(t_lslreceive), (method)0L, A_GIMME, 0);
	
	/* Add class methods */
    class_addmethod(c, (method)lslreceive_assist, "assist", A_CANT, 0);
	
	/* Add attributes */
	
	class_register(CLASS_BOX, c);
	s_lslreceive_class = c;
	
    return 1;
}

void* lslreceive_new(t_symbol* s, long argc, t_atom* argv)
{
    
    // DEBUG print out input args
    // printargs(s, argc, argv);
    // DEBUG print out input args
    
	t_lslreceive* x = (t_lslreceive*) object_alloc(s_lslreceive_class);
	
	if (x != NULL) {
        
        post("lslreceive module for Max/MSP, John Iversen, SCCN, January 2014");
        post("  Adapted from lslaudio by Grace Leslie and Christian Kothe.");
        
        // get nchans, else use default
        if (argc>=1 && argv[0].a_type==A_LONG) {
            x->lsl_nchan = argv[0].a_w.w_long;
            if (x->lsl_nchan < 0) {
                x->lsl_nchan = 1;
                post("Warning: Must specify at least one channel. Defaulting to one channel.");
            }
            if (x->lsl_nchan > MAX_NCHAN) {
                x->lsl_nchan = MAX_NCHAN;
                post("Warning: Limiting the specified large number of channels to the maximum of 2000. Contact the author if this is not sufficient.");
            }
        } else {
            post("Using default number of channels (%d).",DEFAULT_NCHAN);
            x->lsl_nchan = DEFAULT_NCHAN;
        }
        
        // get stream name, or use default
        if (argc>=2 && argv[1].a_type==A_SYM) {
            strncpy(x->lsl_stream_name, atom_getsym(&argv[1])->s_name, MAX_STREAM_NAME_LENGTH);
        } else {
            strncpy(x->lsl_stream_name, DEFAULT_STREAM_NAME, MAX_STREAM_NAME_LENGTH);
            post("Using default stream name (%s)",x->lsl_stream_name);
        }
				
		/* open the stream of interest. We control stream creation, so can explicitly specify the stream rather than
         resolve it.*/
        post("Listening for an AudioControl stream named '%s' with %d channels...",x->lsl_stream_name,x->lsl_nchan);
		x->lsl_info = lsl_create_streaminfo(x->lsl_stream_name,"AudioControl",x->lsl_nchan,LSL_IRREGULAR_RATE,cft_string,"");
        
        /* TODO: use resolver to only match stream with number of channels we're requesting */
        /* problem: MAX won't flush our post messages while this is happening, so user just gets a beachball without knowing why        */
        /*
        char pred[256];
        sprintf(pred, "type='AudioControl' and name='%s' and channel_count=%d","MAX",x->lsl_nchan);
        //post("search predicate: %s",pred);
        int n_found = 0;
        while (~n_found) {
            n_found = lsl_resolve_bypred(&x->lsl_info, 1, pred, 1, 1);
        }
         post("found %d",n_found);
         */        
		
		/* make an inlet to read data from the stream (buffer max. 300 seconds of data, no preference regarding chunking, automatic recovery enabled) */
		x->lsl_inlet = lsl_create_inlet(x->lsl_info, 300, LSL_NO_PREFERENCE, 1);
        
        // TODO on a rainy day: consider checking here if we have a valid stream, getting channel information, etc...
        /*
        int errcode; //we don't do anything with this
        lsl_streaminfo new_info = lsl_get_fullinfo(x->lsl_inlet, 1, &errcode);
         */
        
        
        // actually, this never fails--it's always waiting for the stream until it appears
        if (x->lsl_inlet) {
            
            x->m_outlet[2] = floatout((t_object *)x);
            x->m_outlet[1] = floatout((t_object *)x);
            x->m_outlet[0] = listout((t_object *)x); //outlets are created in order right to left, we'll index l->r :(
            
            x->m_clock  = clock_new((t_object *)x, (method)lslreceive_getSample);
            clock_fdelay(x->m_clock, POLLING_INTERVAL_MS);
        } else {
            post("No matching stream was found. Be sure to specify the number of channels e.g. [lslreceive 10], and that it matches the source.");
        }
	}
	
	return x;
}

void lslreceive_free(t_lslreceive* x)
{
	/* Do any deallocation needed here. */
    clock_unset(x->m_clock);
    object_free(x->m_clock);
    lsl_destroy_inlet(x->lsl_inlet);
}

void lslreceive_assist(t_lslreceive* x, void* b, long m, long a, char* s)
{
	if (m == ASSIST_INLET) {
		switch (a) {
			case 0:
				sprintf(s, "Unused");
				break;
		}
	} 
	else {
		switch (a) {
			case 0:
				sprintf(s, "List of %d strings received from LSL stream '%s'",x->lsl_nchan, x->lsl_stream_name);
				break;
			case 1:
				sprintf(s, "LSL timestamp (sender)");
				break;
			case 2:
				sprintf(s, "LSL timestamp (local)");
				break;
                /*
			case 3:
				sprintf(s, "Volume (1-128)");
				break;
			case 4:
				sprintf(s, "Azimuth (0-360) Clockwise from Northwest Corner");
				break;
			case 5:
				sprintf(s, "Loop On/Off (0/1)");
				break;
			case 6:
				sprintf(s, "On/Off (0/1) ");
				break;
			case 7:
				sprintf(s, "File Tag Number (0-9999)");
				break;
                 */
		}		
	}
}

//polling function to pull a sample
void lslreceive_getSample(t_lslreceive *x)
{
	int errcode; //we don't do anything with this

	x->lsl_timestamp = lsl_pull_sample_str(x->lsl_inlet,x->cursample,x->lsl_nchan,0,&errcode);

    while (x->lsl_timestamp>0)	{
        //post("%f", x->lsl_timestamp);
		//post ("%s  %s  %s  %s  %s  %s  %s  %s",x->cursample[0],x->cursample[1],x->cursample[2],x->cursample[3],x->cursample[4],x->cursample[5],x->cursample[6],x->cursample[7]);
        
		t_atom myList[x->lsl_nchan]; //consider allocating once, rather than at sample		
       
        // return list of strings, for flexibility, and consumer can use [fromsymbol] to convert to numbers
        for (int k=0; k < x->lsl_nchan; ++k) {
			atom_setsym(myList+k,gensym(x->cursample[k]));
		}
        
        x->lsl_local_timestamp = lsl_local_clock(); //time of our output
		outlet_list(x->m_outlet[0],0L,x->lsl_nchan,myList);
        outlet_float(x->m_outlet[1], x->lsl_timestamp);
        outlet_float(x->m_outlet[2], x->lsl_local_timestamp);
        
		x->lsl_timestamp = lsl_pull_sample_str(x->lsl_inlet,x->cursample,x->lsl_nchan,0,&errcode);
	}
	
	clock_fdelay(x->m_clock, POLLING_INTERVAL_MS);
}
