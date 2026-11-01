// Harsher, voice-like test: jitter, shimmer, breath noise, fricatives, vibrato, glides.
// Build: g++ -O2 -std=c++17 -I../Source/dsp realistic_test.cpp -o realistic_test && ./realistic_test
#include "AutotuneEngine.h"
#include <cstdio>
#include <random>

using namespace afina;
static const double kSr = 44100.0;

struct Seg { bool voiced; float dur; float f0a, f0b; float vibCents; };

struct Resonator {
    float a1=0,a2=0,b0=0,y1=0,y2=0;
    void set(float f,float bw){ float r=std::exp(-(float)M_PI*bw/(float)kSr); a1=-2*r*std::cos(2*(float)M_PI*f/(float)kSr); a2=r*r; b0=1-r; }
    float process(float x){ float y=b0*x-a1*y1-a2*y2; y2=y1; y1=y; return y; }
};

int main()
{
    const std::vector<Seg> segs = {
        { true, 0.7f, 220.f*std::pow(2.f,-0.20f/12), 220.f*std::pow(2.f,-0.20f/12), 30 },
        { false, 0.12f, 0, 0, 0 },
        { true, 0.6f, 261.6f*std::pow(2.f,0.25f/12), 261.6f*std::pow(2.f,0.25f/12), 20 },
        { true, 0.3f, 261.6f, 329.6f, 0 },
        { true, 0.6f, 330.f*std::pow(2.f,0.15f/12), 330.f*std::pow(2.f,0.15f/12), 0 },
        { false, 0.10f, 0, 0, 0 },
        { true, 0.7f, 196.f*std::pow(2.f,-0.35f/12), 196.f*std::pow(2.f,-0.35f/12), 40 },
        { true, 0.6f, 110.f*std::pow(2.f,0.20f/12), 110.f*std::pow(2.f,0.20f/12), 25 },
        { false, 0.10f, 0, 0, 0 },
    };

    std::mt19937 rng(7);
    std::normal_distribution<float> gauss(0.f,1.f);
    std::vector<float> x, f0v; std::vector<char> voicedv;
    Resonator r1,r2,r3; r1.set(650,110); r2.set(1100,130); r3.set(2500,170);
    double phase=0; float tilt=0, curPeriodMul=1, curAmp=1, noiseLP=0, prevN=0;
    for (const auto& s : segs) {
        int n=(int)(s.dur*kSr);
        for (int i=0;i<n;++i) {
            float t=(float)i/n;
            float env=std::min({1.f, i/(0.01f*(float)kSr), (n-i)/(0.01f*(float)kSr)});
            float out=0, f0=0;
            if (s.voiced) {
                f0 = s.f0a + (s.f0b-s.f0a)*t;
                float vib = std::pow(2.f, s.vibCents/1200.f*std::sin(2*(float)M_PI*5.5f*(float)i/(float)kSr));
                float fi = f0*vib;
                phase += (double)fi/kSr*curPeriodMul;
                float pulse=0;
                if (phase>=1.0) { phase-=1.0; pulse=curAmp; curPeriodMul=1.f/(1.f+0.007f*gauss(rng)); curAmp=1.f+0.05f*gauss(rng); }
                tilt += 0.35f*(pulse-tilt);
                float w=gauss(rng); noiseLP += 0.2f*(w-noiseLP);
                float src = tilt + 0.02f*noiseLP;
                out = 3.f*(r1.process(src)+0.6f*r2.process(src)+0.3f*r3.process(src))*env;
                f0 = f0*vib;
            } else {
                float w=gauss(rng); float hp=w-prevN; prevN=w;
                out = 0.12f*hp*env;
            }
            x.push_back(out); f0v.push_back(f0); voicedv.push_back(s.voiced);
        }
    }
    x.resize(x.size()+4000,0.f); f0v.resize(x.size(),0.f); voicedv.resize(x.size(),0);

    AutotuneEngine eng; eng.prepare(kSr,1,90.0f);
    PitchCorrector::Params p; for(auto& b:p.notes) b=true; p.retuneMs=0.f;
    eng.setParams(p,1.0f); eng.reset();
    std::vector<float> y(x.size());
    std::vector<AutotuneEngine::Frame> fr;
    for (size_t pos=0; pos<x.size(); pos+=512) {
        int n=(int)std::min<size_t>(512,x.size()-pos);
        const float* in[1]={x.data()+pos}; float* out[1]={y.data()+pos};
        eng.process(in,out,n,[&](const AutotuneEngine::Frame& f){ fr.push_back(f); });
    }
    const int L=eng.latency();
    if(getenv("T")){ float t0=atof(getenv("T")); int h0=(int)((t0-0.08f)*44100/178); int h1=(int)((t0+0.0f)*44100/178); for(int h=h0;h<h1&&h<(int)fr.size();h+=3) std::printf("  hop t=%.3f in=%.2f out=%.2f\n",h*178/44100.f,fr[h].inMidi,fr[h].outMidi); }
    if(getenv("CONTROL")) { for(size_t i=0;i<y.size();++i) y[i]= i>=(size_t)L? x[i-L]:0.f; }

    auto trackF0=[&](const std::vector<float>& sig){ std::vector<float> out(sig.size(),0.f); YinDetector d; d.prepare(kSr,60.f,1000.f); float last=0; for(size_t i=0;i<sig.size();++i){ d.push(sig[i]); if(i%176==175){ float c; last=d.detect(c); } out[i]=last; } return out; };
    auto clarityMean=[&](const std::vector<float>& sig, size_t shift){ YinDetector d; d.prepare(kSr,60.f,1000.f); double sum=0; int cnt=0; for(size_t i=0;i<sig.size();++i){ d.push(sig[i]); if(i%176==175){ float c=0; d.detect(c); size_t k=i-shift; if(i>shift && voicedv[k] && voicedv[k>1500?k-1500:0]) { sum+=c; ++cnt; } } } return cnt? sum/cnt:0.0; };
    std::printf("periodicity (mean clarity, higher=cleaner): input %.3f  output %.3f\n", clarityMean(x,0), clarityMean(y,L));
    const auto yTrack = trackF0(y);
    auto yinF0=[&](const std::vector<float>&, size_t end){ return yTrack[end-1]; };
    auto cents=[](float a,float b){ return 1200.f*std::log2(a/b); };

    std::vector<float> errs; int gross=0, frames=0;
    for (size_t end=4000; end<x.size()-100; end+=700) {
        size_t a=end-1400-L, b=end-L;           // input window that produced this output window
        if (end-1400<(size_t)L) continue;
        bool ok=true; float mn=1e9f,mx=-1e9f,mean=0; int c=0;
        for(size_t i=a;i<b;++i){ if(!voicedv[i]){ok=false;break;} mn=std::min(mn,f0v[i]); mx=std::max(mx,f0v[i]); mean+=f0v[i]; ++c; }
        if(!ok|| cents(mx,mn)>90) continue;       // skip unvoiced / glides
        mean/=c;
        float note=440.f*std::pow(2.f,(std::round(69+12*std::log2(mean/440.f))-69)/12.f);
        float fo=yinF0(y,end);
        ++frames;
        if(fo<=0){ ++gross; if(getenv("V")) std::printf("  t=%.2fs expected %.1f Hz -> LOST\n",(float)end/44100.f,note); continue; }
        float e=cents(fo,note);
        if(std::fabs(e)>400){ ++gross; if(getenv("V")) std::printf("  t=%.2fs expected %.1f Hz -> %.1f Hz (%.0f cents)\n",(float)end/44100.f,note,fo,e); continue; }
        errs.push_back(std::fabs(e)); if(getenv("E")) std::printf("  t=%.2f note %.1f out %.1f err %+.0f c\n",(float)end/44100.f,note,fo,e);
    }
    std::sort(errs.begin(),errs.end());
    auto pct=[&](float q){ return errs.empty()?0.f:errs[(size_t)(q*(errs.size()-1))]; };

    // click metric in voiced regions + unvoiced level
    double maxStepIn=0,maxStepOut=0,eIn=0,eOut=0,uIn=0,uOut=0; size_t cntV=0,cntU=0;
    for(size_t i=L+1;i<x.size();++i){
        size_t k=i-L;
        if(voicedv[k]){ maxStepIn=std::max(maxStepIn,(double)std::fabs(x[k]-x[k-1])); maxStepOut=std::max(maxStepOut,(double)std::fabs(y[i]-y[i-1])); eIn+=x[k]*x[k]; eOut+=y[i]*y[i]; ++cntV; }
        else if(std::fabs(x[k])>1e-4f){ uIn+=x[k]*x[k]; uOut+=y[i]*y[i]; ++cntU; }
    }
    std::printf("frames evaluated: %d | gross errors (octave/lost): %d\n",frames,gross);
    std::printf("pitch error vs nearest note: median %.1f  p90 %.1f  max %.1f cents\n",pct(.5f),pct(.9f),errs.empty()?0.f:errs.back());
    std::printf("max sample step / rms (voiced): input %.2f  output %.2f\n", maxStepIn/std::sqrt(eIn/cntV), maxStepOut/std::sqrt(eOut/cntV));
    std::printf("unvoiced level out/in: %.2f\n", cntU? std::sqrt(uOut/uIn):0.0);
}
