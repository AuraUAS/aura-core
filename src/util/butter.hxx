#ifndef _AURA_BUTTER_FILTER_HXX
#define _AURA_BUTTER_FILTER_HXX

// a class to implement a butterworth filter, based on code and
// explanation here:
//
//     http://www.exstrom.com/journal/sigproc/bwlpf.c

class ButterworthFilter {
    
private:

    int n;
    double *A, *d1, *d2, *w0, *w1, *w2;
    
public:
    
    ButterworthFilter(int order, int samplerate, double cutoff);
    ~ButterworthFilter();
    
    double update( double raw_value );
};


#endif // _AURA_BUTTER_FILTER_HXX
