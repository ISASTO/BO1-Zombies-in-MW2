#pragma once
// Windows Media Audio v2 decoder for the xWMA sounds Black Ops ships.
//
// A C++ port of FFmpeg's WMA decoder (libavcodec/wmadec.c, wma.c,
// wma_common.c), restricted to what xWMA streams use: WMA v2 (format 0x161),
// one or two channels, the bit reservoir, variable block lengths and VLC-coded
// exponents. The output follows FFmpeg sample for sample: whole frames, the
// end-of-stream frame, then the two-frame decoder delay removed.
//
// Copyright (c) 2002-2007 The FFmpeg Project.
// C++ port for BO1MW2, 2026.
//
// This library is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as published by the
// Free Software Foundation; either version 2.1 of the License, or (at your
// option) any later version. It is distributed WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See COPYING.LGPLv2.1 in this directory for the full licence.
#include "wma_tables.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace wmadec {

inline constexpr int kBlockMinBits=7,kBlockMaxBits=11,kBlockMaxSize=1<<kBlockMaxBits;
inline constexpr int kBlockSizes=kBlockMaxBits-kBlockMinBits+1;
inline constexpr int kHighBandMaxSize=16,kMaxChannels=2,kNoiseTableSize=8192;
inline constexpr int kMaxSuperframe=32768;

// MSB-first bit reader. Reads past the data return zero bits, as FFmpeg's
// padded buffers do; the position never runs more than 8 bits past the limit.
class BitReader {
public:
    BitReader()=default;
    BitReader(const std::uint8_t* data,std::size_t bytes,std::size_t bits):data_(data),bytes_(bytes),limit_(bits) {}
    unsigned get(int n) {
        unsigned v=0;
        for(int i=0;i<n;++i) v=(v<<1)|bit(pos_+static_cast<std::size_t>(i));
        advance(n);return v;
    }
    unsigned get1() {const unsigned v=bit(pos_);advance(1);return v;}
    void skip(int n) {advance(n);}
    std::size_t count() const {return pos_;}
    long long left() const {return static_cast<long long>(limit_)-static_cast<long long>(pos_);}
    unsigned peekBit(std::size_t at) const {return bit(pos_+at);}
private:
    unsigned bit(std::size_t at) const {
        if(at>=limit_ || (at>>3)>=bytes_) return 0;
        return (data_[at>>3]>>(7-(at&7)))&1u;
    }
    void advance(int n) {pos_=std::min(pos_+static_cast<std::size_t>(n),limit_+8);}
    const std::uint8_t* data_=nullptr;
    std::size_t bytes_=0,limit_=0,pos_=0;
};

// Prefix-code decoder: a binary trie walked one bit at a time.
class Vlc {
public:
    // code[i] holds bits[i] bits, right aligned; symbol defaults to i.
    bool add(std::uint32_t code,int bits,int symbol) {
        if(bits<=0 || bits>32) return false;
        symbol-=kOffset;
        int node=0;
        for(int b=bits-1;b>=0;--b) {
            const unsigned v=(code>>b)&1u;
            int next=nodes_[static_cast<std::size_t>(node)][v];
            if(b==0) {if(next!=0) return false;nodes_[static_cast<std::size_t>(node)][v]=-(symbol+1);return true;}
            if(next<0) return false;
            if(next==0) {
                next=static_cast<int>(nodes_.size());
                nodes_.push_back({0,0});
                nodes_[static_cast<std::size_t>(node)][v]=next;
            }
            node=next;
        }
        return false;
    }
    void clear() {nodes_.assign(1,{0,0});}
    static constexpr int kInvalid=-0x40000000;
    // The symbol, or kInvalid for a code that is not in the table.
    int decode(BitReader& r) const {
        int node=0;
        for(int depth=0;depth<32;++depth) {
            const int next=nodes_[static_cast<std::size_t>(node)][r.get1()];
            if(next<0) return -next-1+kOffset;
            if(next==0) return kInvalid;
            node=next;
        }
        return kInvalid;
    }
private:
    static constexpr int kOffset=-64;     // symbols may be negative (gain deltas)
    std::vector<std::array<int,2>> nodes_{{0,0}};
};

struct Config {
    int channels=1;
    int sampleRate=44100;
    int bitRate=48000;          // the codec's nominal rate; it selects tables and offsets
    int blockAlign=2230;        // packet size
    int flags2=0x1f;            // xWMA: exponent VLC, bit reservoir, variable blocks
};

// Inverse MDCT: n coefficients -> 2n samples, computed as FFmpeg's
// ff_imdct_calc (pre-rotation, n/4-point complex inverse FFT, post-rotation)
// in double precision:
// y[i] = -scale * sum_k x[k] cos(pi/n (i + 1/2 + n/2)(k + 1/2)).
class Imdct {
public:
    void init(int n,double scale) {
        n_=n;
        const int window=2*n,n4=window/4;
        const double theta=1.0/8.0+(scale<0?n4:0);
        const double s=std::sqrt(std::fabs(scale));
        tcos_.resize(static_cast<std::size_t>(n4));tsin_.resize(static_cast<std::size_t>(n4));
        for(int i=0;i<n4;++i) {
            const double alpha=2*M_PI*(i+theta)/window;
            tcos_[static_cast<std::size_t>(i)]=-std::cos(alpha)*s;tsin_[static_cast<std::size_t>(i)]=-std::sin(alpha)*s;
        }
        const int m=n4;             // FFT size (complex points)
        roots_.resize(static_cast<std::size_t>(std::max(1,m/2)));
        for(int k=0;k<m/2;++k) roots_[static_cast<std::size_t>(k)]=std::polar(1.0,2.0*M_PI*k/m);
        z_.resize(static_cast<std::size_t>(m));
        rev_.resize(static_cast<std::size_t>(m));
        int bits=0;while((1<<bits)<m) ++bits;
        for(int k=0;k<m;++k) {int r=0;for(int b=0;b<bits;++b) if(k&(1<<b)) r|=1<<(bits-1-b);rev_[static_cast<std::size_t>(k)]=r;}
    }
    void run(float* out,const float* in) {
        const int window=2*n_,n2=window/2,n4=window/4,n8=window/8;
        const float* in1=in;const float* in2=in+n2-1;
        for(int k=0;k<n4;++k) {
            const double are=in2[-2*k],aim=in1[2*k],bre=tcos_[static_cast<std::size_t>(k)],bim=tsin_[static_cast<std::size_t>(k)];
            z_[static_cast<std::size_t>(rev_[static_cast<std::size_t>(k)])]={are*bre-aim*bim,are*bim+aim*bre};
        }
        Fft();
        for(int k=0;k<n8;++k) {
            const auto a=z_[static_cast<std::size_t>(n8-k-1)],b=z_[static_cast<std::size_t>(n8+k)];
            const double s0=tsin_[static_cast<std::size_t>(n8-k-1)],c0=tcos_[static_cast<std::size_t>(n8-k-1)];
            const double s1=tsin_[static_cast<std::size_t>(n8+k)],c1=tcos_[static_cast<std::size_t>(n8+k)];
            const double r0=a.imag()*s0-a.real()*c0,i1=a.imag()*c0+a.real()*s0;
            const double r1=b.imag()*s1-b.real()*c1,i0=b.imag()*c1+b.real()*s1;
            z_[static_cast<std::size_t>(n8-k-1)]={r0,i0};z_[static_cast<std::size_t>(n8+k)]={r1,i1};
        }
        float* half=out+n4;
        for(int k=0;k<n4;++k) {half[2*k]=static_cast<float>(z_[static_cast<std::size_t>(k)].real());half[2*k+1]=static_cast<float>(z_[static_cast<std::size_t>(k)].imag());}
        for(int k=0;k<n4;++k) {out[k]=-out[n2-k-1];out[window-k-1]=out[n2+k];}
    }
private:
    // In-place radix-2 inverse FFT of the bit-reversed z_.
    void Fft() {
        const std::size_t m=z_.size();
        for(std::size_t len=2;len<=m;len<<=1) {
            const std::size_t step=m/len;
            for(std::size_t i=0;i<m;i+=len)
                for(std::size_t j=0;j<len/2;++j) {
                    const auto w=roots_[j*step];
                    const auto u=z_[i+j],v=z_[i+j+len/2]*w;
                    z_[i+j]=u+v;z_[i+j+len/2]=u-v;
                }
        }
    }
    int n_=0;
    std::vector<double> tcos_,tsin_;
    std::vector<std::complex<double>> roots_,z_;
    std::vector<int> rev_;
};

// Reference IMDCT by the definition (tests).
inline void ImdctDirect(float* out,const float* in,int n,double scale) {
    for(int i=0;i<2*n;++i) {
        double acc=0;
        for(int k=0;k<n;++k) acc+=in[k]*std::cos(M_PI/n*(i+0.5+n/2.0)*(k+0.5));
        out[i]=static_cast<float>(-scale*acc);
    }
}

class Decoder {
public:
    bool init(const Config& c,std::string* error=nullptr) {
        auto fail=[&](const char* text){if(error)*error=text;return false;};
        if(c.sampleRate<=0 || c.sampleRate>50000 || c.channels<1 || c.channels>2 || c.bitRate<=0 || c.blockAlign<=0)
            return fail("unsupported WMA stream parameters");
        cfg_=c;
        useExpVlc_=c.flags2&1;useBitReservoir_=(c.flags2&2)!=0;useVariableBlockLen_=(c.flags2&4)!=0;
        if(!useExpVlc_) return fail("LSP-coded WMA exponents are not supported");
        maxExponent_.fill(1.f);
        // ff_wma_init, version 2.
        if(c.sampleRate<=16000) frameLenBits_=9;
        else if(c.sampleRate<=22050) frameLenBits_=10;
        else frameLenBits_=11;
        nextBlockLenBits_=prevBlockLenBits_=blockLenBits_=frameLenBits_;
        frameLen_=1<<frameLenBits_;
        if(useVariableBlockLen_) {
            int nb=((c.flags2>>3)&3)+1;
            if(c.bitRate/c.channels>=32000) nb+=2;
            const int nbMax=frameLenBits_-kBlockMinBits;
            nbBlockSizes_=std::min(nb,nbMax)+1;
        } else nbBlockSizes_=1;
        useNoiseCoding_=true;
        float highFreq=c.sampleRate*0.5f;
        int sampleRate1=c.sampleRate;
        if(sampleRate1>=44100) sampleRate1=44100;
        else if(sampleRate1>=22050) sampleRate1=22050;
        else if(sampleRate1>=16000) sampleRate1=16000;
        else if(sampleRate1>=11025) sampleRate1=11025;
        else if(sampleRate1>=8000) sampleRate1=8000;
        const float bps=static_cast<float>(c.bitRate)/static_cast<float>(c.channels*c.sampleRate);
        byteOffsetBits_=Log2(static_cast<int>(bps*frameLen_/8.0+0.5))+2;
        if(byteOffsetBits_+3>25) return fail("WMA byte offset too large");
        float bps1=bps;
        if(c.channels==2) bps1=bps*1.6f;
        if(sampleRate1==44100) {
            if(bps1>=0.61f) useNoiseCoding_=false;
            else highFreq=highFreq*0.4f;
        } else if(sampleRate1==22050) {
            if(bps1>=1.16f) useNoiseCoding_=false;
            else if(bps1>=0.72f) highFreq=highFreq*0.7f;
            else highFreq=highFreq*0.6f;
        } else if(sampleRate1==16000) {
            highFreq=bps>0.5f?highFreq*0.5f:highFreq*0.3f;
        } else if(sampleRate1==11025) highFreq=highFreq*0.7f;
        else if(sampleRate1==8000) {
            if(bps<=0.625f) highFreq=highFreq*0.5f;
            else if(bps>0.75f) useNoiseCoding_=false;
            else highFreq=highFreq*0.65f;
        } else {
            if(bps>=0.8f) highFreq=highFreq*0.75f;
            else if(bps>=0.6f) highFreq=highFreq*0.6f;
            else highFreq=highFreq*0.5f;
        }
        coefsStart_=0;
        for(int k=0;k<nbBlockSizes_;++k) {
            const int blockLen=frameLen_>>k;
            const std::uint8_t* table=nullptr;
            const int a=frameLenBits_-kBlockMinBits-k;
            if(a<3) {
                if(c.sampleRate>=44100) table=tables::exponent_band_44100[a];
                else if(c.sampleRate>=32000) table=tables::exponent_band_32000[a];
                else if(c.sampleRate>=22050) table=tables::exponent_band_22050[a];
            }
            if(table) {
                const int n=*table++;
                for(int i=0;i<n;++i) exponentBands_[k][i]=table[i];
                exponentSizes_[k]=n;
            } else {
                int j=0,lpos=0;
                for(int i=0;i<25;++i) {
                    const int f=tables::critical_freqs[i];
                    int pos=((blockLen*2*f)+(c.sampleRate<<1))/(4*c.sampleRate);
                    pos<<=2;
                    if(pos>blockLen) pos=blockLen;
                    if(pos>lpos) exponentBands_[k][j++]=static_cast<std::uint16_t>(pos-lpos);
                    if(pos>=blockLen) break;
                    lpos=pos;
                }
                exponentSizes_[k]=j;
            }
            coefsEnd_[k]=(frameLen_-((frameLen_*9)/100))>>k;
            highBandStart_[k]=static_cast<int>((blockLen*2*highFreq)/c.sampleRate+0.5);
            const int n=exponentSizes_[k];
            int j=0,pos=0;
            for(int i=0;i<n;++i) {
                int start=pos;pos+=exponentBands_[k][i];int end=pos;
                if(start<highBandStart_[k]) start=highBandStart_[k];
                if(end>coefsEnd_[k]) end=coefsEnd_[k];
                if(end>start) {
                    if(j>=kHighBandMaxSize) return fail("too many WMA high bands");
                    exponentHighBands_[k][j++]=end-start;
                }
            }
            exponentHighSizes_[k]=j;
        }
        for(int i=0;i<nbBlockSizes_;++i) {
            const int n=1<<(frameLenBits_-i);
            auto& w=windows_[i];w.resize(static_cast<std::size_t>(n));
            for(int k=0;k<n;++k) w[static_cast<std::size_t>(k)]=std::sin(static_cast<float>((k+0.5)*(M_PI/(2.0*n))));
            mdct_[i].init(n,1.0/32768.0);
        }
        resetBlockLengths_=true;
        if(useNoiseCoding_) {
            noiseMult_=0.02f;
            unsigned seed=1;
            const float norm=static_cast<float>((1.0/static_cast<float>(1LL<<31))*std::sqrt(3.0)*noiseMult_);
            for(int i=0;i<kNoiseTableSize;++i) {seed=seed*314159u+1u;noiseTable_[i]=static_cast<float>(static_cast<int>(seed))*norm;}
            hgain_.clear();
            std::uint32_t code=0;
            for(const auto& e:tables::hgain_hufftab) {
                const int len=e[1];
                if(!hgain_.add(code>>(32-len),len,e[0]-18)) return fail("bad WMA gain table");
                code+=1u<<(32-len);
            }
        }
        exp_.clear();
        for(int i=0;i<121;++i) if(!exp_.add(tables::scalefactor_code[i],tables::scalefactor_bits[i],i)) return fail("bad WMA exponent table");
        int table=2;
        if(c.sampleRate>=32000) {
            if(bps1<0.72f) table=0;
            else if(bps1<1.16f) table=1;
        }
        for(int t=0;t<2;++t) {
            const auto& src=tables::coef_tables[table*2+t];
            coef_[t].clear();
            for(int i=0;i<src.n;++i) if(!coef_[t].add(src.codes[i],src.bits[i],i)) return fail("bad WMA coefficient table");
            run_[t].assign(static_cast<std::size_t>(src.n),0);level_[t].assign(static_cast<std::size_t>(src.n),0.f);
            int i=2,level=1,k=0;
            while(i<src.n) {
                const int l=k<src.levelCount?src.levels[k]:0;++k;
                for(int j=0;j<l && i<src.n;++j) {run_[t][static_cast<std::size_t>(i)]=static_cast<std::uint16_t>(j);level_[t][static_cast<std::size_t>(i)]=static_cast<float>(level);++i;}
                ++level;
                if(k>src.levelCount+1) return fail("bad WMA level table");
            }
        }
        for(auto& f:frameOut_) f.fill(0.f);
        lastSuperframeLen_=0;lastBitOffset_=0;eofDone_=false;
        exponentsInitialized_.fill(false);
        return true;
    }

    int frameLength() const {return frameLen_;}
    int channels() const {return cfg_.channels;}

    // One packet (blockAlign bytes). Appends whole frames, interleaved floats.
    // Returns false on a damaged packet (FFmpeg then resets the reservoir).
    bool decodePacket(const std::uint8_t* buf,std::size_t size,std::vector<float>& out) {
        if(size<static_cast<std::size_t>(cfg_.blockAlign)) return false;
        const int bufSize=cfg_.blockAlign;
        BitReader gb(buf,static_cast<std::size_t>(bufSize),static_cast<std::size_t>(bufSize)*8);
        int nbFrames=1;
        if(useBitReservoir_) {
            gb.skip(4);
            nbFrames=static_cast<int>(gb.get(4))-(lastSuperframeLen_<=0?1:0);
            if(nbFrames<=0) {
                const bool isError=nbFrames<0 || gb.left()<=8;
                if(isError) return fail();
                if(lastSuperframeLen_+bufSize-1>kMaxSuperframe) return fail();
                std::uint8_t* q=lastSuperframe_.data()+lastSuperframeLen_;
                for(int len=bufSize-1;len>0;--len) *q++=static_cast<std::uint8_t>(gb.get(8));
                std::memset(q,0,64);
                lastSuperframeLen_+=8*bufSize-8;
                return true;
            }
        }
        if(useBitReservoir_) {
            const int bitOffset=static_cast<int>(gb.get(byteOffsetBits_+3));
            if(bitOffset>gb.left()) return fail();
            if(lastSuperframeLen_>0) {
                if(lastSuperframeLen_+((bitOffset+7)>>3)>kMaxSuperframe) return fail();
                std::uint8_t* q=lastSuperframe_.data()+lastSuperframeLen_;
                int len=bitOffset;
                while(len>7) {*q++=static_cast<std::uint8_t>(gb.get(8));len-=8;}
                if(len>0) *q++=static_cast<std::uint8_t>(gb.get(len)<<(8-len));
                std::memset(q,0,64);
                gb_=BitReader(lastSuperframe_.data(),lastSuperframe_.size(),static_cast<std::size_t>(lastSuperframeLen_)*8+static_cast<std::size_t>(bitOffset));
                if(lastBitOffset_>0) gb_.skip(lastBitOffset_);
                if(!decodeFrame(out)) return fail();
                --nbFrames;
            }
            const int pos=bitOffset+4+4+byteOffsetBits_+3;
            if(pos>=kMaxSuperframe*8 || pos>bufSize*8) return fail();
            gb_=BitReader(buf+(pos>>3),static_cast<std::size_t>(bufSize-(pos>>3)),static_cast<std::size_t>(bufSize-(pos>>3))*8);
            if(pos&7) gb_.skip(pos&7);
            resetBlockLengths_=true;
            for(int i=0;i<nbFrames;++i) if(!decodeFrame(out)) return fail();
            int end=static_cast<int>(gb_.count())+((bitOffset+4+4+byteOffsetBits_+3)&~7);
            lastBitOffset_=end&7;
            end>>=3;
            const int len=bufSize-end;
            if(len>kMaxSuperframe || len<0) return fail();
            lastSuperframeLen_=len;
            std::memcpy(lastSuperframe_.data(),buf+end,static_cast<std::size_t>(len));
        } else {
            gb_=gb;
            if(!decodeFrame(out)) return fail();
        }
        return true;
    }
    // The frame still held for overlap, emitted once at the end of the stream.
    void flush(std::vector<float>& out) {
        if(eofDone_) return;
        emit(out);
        lastSuperframeLen_=0;eofDone_=true;
    }

private:
    static int Log2(int v) {int r=0;while(v>1){v>>=1;++r;}return r;}
    bool fail() {lastSuperframeLen_=0;return false;}

    void emit(std::vector<float>& out) {
        const int ch=cfg_.channels;
        const std::size_t base=out.size();
        out.resize(base+static_cast<std::size_t>(frameLen_*ch));
        for(int i=0;i<frameLen_;++i) for(int c=0;c<ch;++c) out[base+static_cast<std::size_t>(i*ch+c)]=frameOut_[c][static_cast<std::size_t>(i)];
    }
    bool decodeFrame(std::vector<float>& out) {
        blockNum_=0;blockPos_=0;
        for(;;) {
            const int r=decodeBlock();
            if(r<0) return false;
            if(r) break;
        }
        emit(out);
        for(int c=0;c<cfg_.channels;++c)
            std::memmove(frameOut_[c].data(),frameOut_[c].data()+frameLen_,static_cast<std::size_t>(frameLen_)*sizeof(float));
        return true;
    }
    bool decodeExpVlc(int ch) {
        const std::uint16_t* ptr=exponentBands_[frameLenBits_-blockLenBits_];
        float* q=exponents_[ch].data();
        float* qEnd=q+blockLen_;
        float maxScale=0.f;
        int lastExp=36;
        while(q<qEnd) {
            const int code=exp_.decode(gb_);
            if(code==Vlc::kInvalid) return false;
            lastExp+=code-60;
            if(static_cast<unsigned>(lastExp+60)>=156u) return false;
            const float v=tables::pow_tab[lastExp+60];
            if(v>maxScale) maxScale=v;
            int n=*ptr++;
            if(n<=0 || q+n>qEnd) {if(n<=0) return false;n=static_cast<int>(qEnd-q);}
            for(int i=0;i<n;++i) *q++=v;
        }
        maxExponent_[ch]=maxScale;
        return true;
    }
    bool runLevelDecode(int t,float* ptr,int numCoefs,int coefNbBits) {
        const unsigned mask=static_cast<unsigned>(blockLen_-1);
        int offset=0;
        for(;offset<numCoefs;++offset) {
            const int code=coef_[t].decode(gb_);
            if(code==Vlc::kInvalid) return false;
            if(code>1) {
                offset+=run_[t][static_cast<std::size_t>(code)];
                const bool negative=gb_.get1()==0;
                const float level=level_[t][static_cast<std::size_t>(code)];
                ptr[static_cast<unsigned>(offset)&mask]=negative?-level:level;
            } else if(code==1) break;
            else {
                const int level=static_cast<int>(gb_.get(coefNbBits));
                offset+=static_cast<int>(gb_.get(frameLenBits_));
                const bool negative=gb_.get1()==0;
                ptr[static_cast<unsigned>(offset)&mask]=static_cast<float>(negative?-level:level);
            }
        }
        return offset<=numCoefs;
    }
    static int TotalGainToBits(int g) {return g<15?13:g<32?12:g<40?11:g<45?10:9;}

    void window(float* out) {
        const float* in=output_.data();
        if(blockLenBits_<=prevBlockLenBits_) {
            const int bsize=frameLenBits_-blockLenBits_;
            const auto& w=windows_[bsize];
            for(int i=0;i<blockLen_;++i) out[i]=in[i]*w[static_cast<std::size_t>(i)]+out[i];
        } else {
            const int len=1<<prevBlockLenBits_;
            const int n=(blockLen_-len)/2;
            const int bsize=frameLenBits_-prevBlockLenBits_;
            const auto& w=windows_[bsize];
            for(int i=0;i<len;++i) out[n+i]=in[n+i]*w[static_cast<std::size_t>(i)]+out[n+i];
            std::memcpy(out+n+len,in+n+len,static_cast<std::size_t>(n)*sizeof(float));
        }
        out+=blockLen_;in+=blockLen_;
        if(blockLenBits_<=nextBlockLenBits_) {
            const int bsize=frameLenBits_-blockLenBits_;
            const auto& w=windows_[bsize];
            for(int i=0;i<blockLen_;++i) out[i]=in[i]*w[static_cast<std::size_t>(blockLen_-1-i)];
        } else {
            const int len=1<<nextBlockLenBits_;
            const int n=(blockLen_-len)/2;
            const int bsize=frameLenBits_-nextBlockLenBits_;
            const auto& w=windows_[bsize];
            std::memcpy(out,in,static_cast<std::size_t>(n)*sizeof(float));
            for(int i=0;i<len;++i) out[n+i]=in[n+i]*w[static_cast<std::size_t>(len-1-i)];
            std::memset(out+n+len,0,static_cast<std::size_t>(n)*sizeof(float));
        }
    }

    // 0: more blocks in this frame, 1: last block, -1: error.
    int decodeBlock() {
        const int channels=cfg_.channels;
        if(useVariableBlockLen_) {
            const int n=Log2(nbBlockSizes_-1)+1;
            if(resetBlockLengths_) {
                resetBlockLengths_=false;
                int v=static_cast<int>(gb_.get(n));
                if(v>=nbBlockSizes_) return -1;
                prevBlockLenBits_=frameLenBits_-v;
                v=static_cast<int>(gb_.get(n));
                if(v>=nbBlockSizes_) return -1;
                blockLenBits_=frameLenBits_-v;
            } else {
                prevBlockLenBits_=blockLenBits_;
                blockLenBits_=nextBlockLenBits_;
            }
            const int v=static_cast<int>(gb_.get(n));
            if(v>=nbBlockSizes_) return -1;
            nextBlockLenBits_=frameLenBits_-v;
        } else {
            nextBlockLenBits_=prevBlockLenBits_=blockLenBits_=frameLenBits_;
        }
        if(frameLenBits_-blockLenBits_>=nbBlockSizes_) return -1;
        blockLen_=1<<blockLenBits_;
        if(blockPos_+blockLen_>frameLen_) return -1;
        if(channels==2) msStereo_=gb_.get1()!=0;
        bool any=false;
        for(int ch=0;ch<channels;++ch) {channelCoded_[ch]=gb_.get1()!=0;any=any||channelCoded_[ch];}
        const int bsize=frameLenBits_-blockLenBits_;
        if(any) {
            int totalGain=1;
            for(;;) {
                if(gb_.left()<7) return -1;
                const int a=static_cast<int>(gb_.get(7));
                totalGain+=a;
                if(a!=127) break;
            }
            const int coefNbBits=TotalGainToBits(totalGain);
            const int n=coefsEnd_[bsize]-coefsStart_;
            std::array<int,kMaxChannels> nbCoefs{n,n};
            if(useNoiseCoding_) {
                for(int ch=0;ch<channels;++ch) if(channelCoded_[ch]) {
                    for(int i=0;i<exponentHighSizes_[bsize];++i) {
                        highBandCoded_[ch][i]=gb_.get1()!=0;
                        if(highBandCoded_[ch][i]) nbCoefs[ch]-=exponentHighBands_[bsize][i];
                    }
                }
                for(int ch=0;ch<channels;++ch) if(channelCoded_[ch]) {
                    int val=static_cast<int>(0x80000000u);
                    for(int i=0;i<exponentHighSizes_[bsize];++i) if(highBandCoded_[ch][i]) {
                        if(val==static_cast<int>(0x80000000u)) val=static_cast<int>(gb_.get(7))-19;
                        else {const int d=hgain_.decode(gb_);if(d==Vlc::kInvalid) return -1;val+=d;}
                        highBandValues_[ch][i]=val;
                    }
                }
            }
            if(blockLenBits_==frameLenBits_ || gb_.get1()) {
                for(int ch=0;ch<channels;++ch) if(channelCoded_[ch]) {
                    if(!decodeExpVlc(ch)) return -1;
                    exponentsBsize_[ch]=bsize;exponentsInitialized_[ch]=true;
                }
            }
            for(int ch=0;ch<channels;++ch) if(channelCoded_[ch] && !exponentsInitialized_[ch]) return -1;
            for(int ch=0;ch<channels;++ch) {
                if(!channelCoded_[ch]) continue;
                const int t=(ch==1 && msStereo_)?1:0;
                std::fill_n(coefs1_[ch].begin(),blockLen_,0.f);
                if(!runLevelDecode(t,coefs1_[ch].data(),nbCoefs[ch],coefNbBits)) return -1;
            }
            const float mdctNorm=1.0f/static_cast<float>(blockLen_/2);
            for(int ch=0;ch<channels;++ch) {
                if(!channelCoded_[ch]) continue;
                const float* c1=coefs1_[ch].data();
                const float* exponents=exponents_[ch].data();
                const int esize=exponentsBsize_[ch];
                float mult=static_cast<float>(std::pow(10.0,totalGain*0.05)/maxExponent_[ch]);
                mult*=mdctNorm;
                float* coefs=coefs_[ch].data();
                if(useNoiseCoding_) {
                    float mult1=mult;
                    for(int i=0;i<coefsStart_;++i) {
                        *coefs++=noiseTable_[noiseIndex_]*exponents[i<<bsize>>esize]*mult1;
                        noiseIndex_=(noiseIndex_+1)&(kNoiseTableSize-1);
                    }
                    const int n1=exponentHighSizes_[bsize];
                    std::array<float,kHighBandMaxSize> expPower{};
                    exponents=exponents_[ch].data()+(highBandStart_[bsize]<<bsize>>esize);
                    int lastHighBand=0;
                    for(int j=0;j<n1;++j) {
                        const int nn=exponentHighBands_[frameLenBits_-blockLenBits_][j];
                        if(highBandCoded_[ch][j]) {
                            float e2=0.f;
                            for(int i=0;i<nn;++i) {const float v=exponents[i<<bsize>>esize];e2+=v*v;}
                            expPower[static_cast<std::size_t>(j)]=e2/nn;lastHighBand=j;
                        }
                        exponents+=nn<<bsize>>esize;
                    }
                    exponents=exponents_[ch].data()+(coefsStart_<<bsize>>esize);
                    for(int j=-1;j<n1;++j) {
                        const int nn=j<0?highBandStart_[bsize]-coefsStart_:exponentHighBands_[frameLenBits_-blockLenBits_][j];
                        if(j>=0 && highBandCoded_[ch][j]) {
                            mult1=std::sqrt(expPower[static_cast<std::size_t>(j)]/expPower[static_cast<std::size_t>(lastHighBand)]);
                            mult1=static_cast<float>(mult1*std::pow(10.0,highBandValues_[ch][j]*0.05));
                            mult1=mult1/(maxExponent_[ch]*noiseMult_);
                            mult1*=mdctNorm;
                            for(int i=0;i<nn;++i) {
                                const float noise=noiseTable_[noiseIndex_];
                                noiseIndex_=(noiseIndex_+1)&(kNoiseTableSize-1);
                                *coefs++=noise*exponents[i<<bsize>>esize]*mult1;
                            }
                        } else {
                            for(int i=0;i<nn;++i) {
                                const float noise=noiseTable_[noiseIndex_];
                                noiseIndex_=(noiseIndex_+1)&(kNoiseTableSize-1);
                                *coefs++=((*c1++)+noise)*exponents[i<<bsize>>esize]*mult;
                            }
                        }
                        exponents+=nn<<bsize>>esize;
                    }
                    const int nn=blockLen_-coefsEnd_[bsize];
                    mult1=mult*exponents[(-(1<<bsize))>>esize];
                    for(int i=0;i<nn;++i) {
                        *coefs++=noiseTable_[noiseIndex_]*mult1;
                        noiseIndex_=(noiseIndex_+1)&(kNoiseTableSize-1);
                    }
                } else {
                    for(int i=0;i<coefsStart_;++i) *coefs++=0.f;
                    for(int i=0;i<nbCoefs[ch];++i) *coefs++=c1[i]*exponents[i<<bsize>>esize]*mult;
                    for(int i=0;i<blockLen_-coefsEnd_[bsize];++i) *coefs++=0.f;
                }
            }
            if(msStereo_ && channelCoded_[1]) {
                if(!channelCoded_[0]) {std::fill_n(coefs_[0].begin(),blockLen_,0.f);channelCoded_[0]=true;}
                for(int i=0;i<blockLen_;++i) {
                    const float t=coefs_[0][static_cast<std::size_t>(i)]-coefs_[1][static_cast<std::size_t>(i)];
                    coefs_[0][static_cast<std::size_t>(i)]+=coefs_[1][static_cast<std::size_t>(i)];
                    coefs_[1][static_cast<std::size_t>(i)]=t;
                }
            }
        }
        for(int ch=0;ch<channels;++ch) {
            const int n4=blockLen_/2;
            if(any && channelCoded_[ch]) mdct_[bsize].run(output_.data(),coefs_[ch].data());
            else if(!(msStereo_ && ch==1)) output_.fill(0.f);
            const int index=(frameLen_/2)+blockPos_-n4;
            window(&frameOut_[ch][static_cast<std::size_t>(index)]);
        }
        ++blockNum_;
        blockPos_+=blockLen_;
        return blockPos_>=frameLen_?1:0;
    }

    Config cfg_;
    bool useExpVlc_=true,useBitReservoir_=true,useVariableBlockLen_=true,useNoiseCoding_=false;
    int byteOffsetBits_=0,frameLen_=0,frameLenBits_=0,nbBlockSizes_=1;
    bool resetBlockLengths_=true;
    int blockLenBits_=0,nextBlockLenBits_=0,prevBlockLenBits_=0,blockLen_=0,blockNum_=0,blockPos_=0;
    bool msStereo_=false;
    std::array<bool,kMaxChannels> channelCoded_{};
    std::array<int,kMaxChannels> exponentsBsize_{};
    std::array<bool,kMaxChannels> exponentsInitialized_{};
    std::array<std::array<float,kBlockMaxSize>,kMaxChannels> exponents_{},coefs1_{},coefs_{};
    std::array<float,kMaxChannels> maxExponent_{};
    std::array<float,kBlockMaxSize*2> output_{};
    std::array<std::array<float,kBlockMaxSize*2>,kMaxChannels> frameOut_{};
    int exponentSizes_[kBlockSizes]{};
    std::uint16_t exponentBands_[kBlockSizes][25]{};
    int highBandStart_[kBlockSizes]{},coefsStart_=0,coefsEnd_[kBlockSizes]{},exponentHighSizes_[kBlockSizes]{};
    int exponentHighBands_[kBlockSizes][kHighBandMaxSize]{};
    bool highBandCoded_[kMaxChannels][kHighBandMaxSize]{};
    int highBandValues_[kMaxChannels][kHighBandMaxSize]{};
    Vlc exp_,hgain_,coef_[2];
    std::vector<std::uint16_t> run_[2];
    std::vector<float> level_[2];
    std::array<std::vector<float>,kBlockSizes> windows_;
    Imdct mdct_[kBlockSizes];
    std::array<std::uint8_t,kMaxSuperframe+64> lastSuperframe_{};
    int lastBitOffset_=0,lastSuperframeLen_=0;
    std::array<float,kNoiseTableSize> noiseTable_{};
    int noiseIndex_=0;
    float noiseMult_=0.02f;
    bool eofDone_=false;
    BitReader gb_;
};

// Float sample -> 16-bit PCM exactly as FFmpeg's converter does.
inline std::int16_t ToPcm16(float v) {
    const long r=std::lrintf(v*32768.f);
    return static_cast<std::int16_t>(std::clamp<long>(r,-32768,32767));
}

// A whole xWMA stream: every packet, the end-of-stream frame, minus the
// decoder's two-frame delay. `damaged` counts packets that failed to decode.
inline bool DecodeStream(const Config& c,const std::uint8_t* data,std::size_t size,std::vector<std::int16_t>& pcm,
                         int* damaged=nullptr,std::string* error=nullptr) {
    auto dec=std::make_unique<Decoder>();
    if(!dec->init(c,error)) return false;
    std::vector<float> f;
    int bad=0;
    for(std::size_t at=0;at+static_cast<std::size_t>(c.blockAlign)<=size;at+=static_cast<std::size_t>(c.blockAlign))
        if(!dec->decodePacket(data+at,static_cast<std::size_t>(c.blockAlign),f)) ++bad;
    dec->flush(f);
    if(damaged) *damaged=bad;
    const std::size_t skip=static_cast<std::size_t>(dec->frameLength())*2*static_cast<std::size_t>(c.channels);
    pcm.clear();
    if(f.size()>skip) {pcm.reserve(f.size()-skip);for(std::size_t i=skip;i<f.size();++i) pcm.push_back(ToPcm16(f[i]));}
    return true;
}

} // namespace wmadec
