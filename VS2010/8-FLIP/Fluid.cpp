/*
Copyright (c) 2013 Benedikt Bitterli

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/

#define _USE_MATH_DEFINES
#include <cmath>
#include <GL/freeglut.h>

#include <algorithm>
#include <stdint.h>
#include <stdio.h>
#include <vector> 
#include <stack>
 

using namespace std;

/* Bit twiddling floating point rand function. Returns a value in [0, 1) */
float frand() {
	static uint32_t seed = 0xBA5EBA11;
	seed = (seed*1103515245 + 12345) & 0x7FFFFFFF;

	union { uint32_t i; float f; } unionHack;

	unionHack.i = (seed >> 8) | 0x3F800000;
	return unionHack.f - 1.0f;
}

template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

template <typename T> int nsgn(T val) {
    return (val < T(0) ? -1 : 1);
}

double length(double x, double y) {
    return sqrt(x*x + y*y);
}

double cubicPulse(double x) {
    x = min(fabs(x), 1.0);
    return 1.0 - x*x*(3.0 - 2.0*x);
}

void rotate(double &x, double &y, double phi) {
    double tmpX = x, tmpY = y;
    x =  cos(phi)*tmpX + sin(phi)*tmpY;
    y = -sin(phi)*tmpX + cos(phi)*tmpY;
}

double triangleOccupancy(double out1, double in, double out2) {
	return 0.5*in*in/((out1 - in)*(out2 - in));
}

double trapezoidOccupancy(double out1, double out2, double in1, double in2) {
	return 0.5*(-in1/(out1 - in1) - in2/(out2 - in2));
}

double occupancy(double d11, double d12, double d21, double d22) {
	double ds[] = {d11, d12, d22, d21};
    
	uint8_t b = 0;
	for (int i = 3; i >= 0; i--)
		b = (b << 1) | (ds[i] < 0.0 ? 1 : 0);
    
	switch (b) {
	case 0x0: return 0.0;

	case 0x1: return triangleOccupancy(d21, d11, d12);
	case 0x2: return triangleOccupancy(d11, d12, d22);
	case 0x4: return triangleOccupancy(d12, d22, d21);
	case 0x8: return triangleOccupancy(d22, d21, d11);

	case 0xE: return 1.0 - triangleOccupancy(-d21, -d11, -d12);
	case 0xD: return 1.0 - triangleOccupancy(-d11, -d12, -d22);
	case 0xB: return 1.0 - triangleOccupancy(-d12, -d22, -d21);
	case 0x7: return 1.0 - triangleOccupancy(-d22, -d21, -d11);
        
	case 0x3: return trapezoidOccupancy(d21, d22, d11, d12);
	case 0x6: return trapezoidOccupancy(d11, d21, d12, d22);
	case 0x9: return trapezoidOccupancy(d12, d22, d11, d21);
	case 0xC: return trapezoidOccupancy(d11, d12, d21, d22);

	case 0x5: return triangleOccupancy(d11, d12, d22) +
		             triangleOccupancy(d22, d21, d11);
	case 0xA: return triangleOccupancy(d21, d11, d12) +
		             triangleOccupancy(d12, d22, d21);

	case 0xF: return 1.0;
	}
    
    return 0.0;
}

enum CellType {
    CELL_FLUID,
    CELL_SOLID,
    CELL_EMPTY
};

class SolidBody {
protected:
    double _posX;
    double _posY;
    double _scaleX;
    double _scaleY;
    double _theta;
    
    double _velX;
    double _velY;
    double _velTheta;
    
    void globalToLocal(double &x, double &y) const {
        x -= _posX;
        y -= _posY;
        rotate(x, y, -_theta);
        x /= _scaleX;
        y /= _scaleY;
    }
    
    void localToGlobal(double &x, double &y) const {
        x *= _scaleX;
        y *= _scaleY;
        rotate(x, y, _theta);
        x += _posX;
        y += _posY;
    }

public:
    SolidBody(double posX, double posY, double scaleX, double scaleY,
        double theta, double velX, double velY, double velTheta) :
            _posX(posX), _posY(posY), _scaleX(scaleX), _scaleY(scaleY),
            _theta(theta), _velX(velX), _velY(velY), _velTheta(velTheta) {}
                
    virtual ~SolidBody() {};
    

    virtual double distance(double x, double y) const = 0;
    virtual void closestSurfacePoint(double &x, double &y) const = 0;
    virtual void distanceNormal(double &nx, double &ny, double x, double y) const = 0;
    
    double velocityX(double x, double y) const {
        return (_posY - y)*_velTheta + _velX;
    }
    
    double velocityY(double x, double y) const {
        return (x - _posX)*_velTheta + _velY;
    }
    
    void velocity(double &vx, double &vy, double x, double y) const {
        vx = velocityX(x, y);
        vy = velocityY(x, y);
    }
    
    void update(double timestep) {
        _posX  += _velX*timestep;
        _posY  += _velY*timestep;
        _theta += _velTheta*timestep;
    }
};

class SolidBox: public SolidBody {
public:
    
    SolidBox(double x, double y, double sx, double sy, double t, double vx, double vy, double vt) :
        SolidBody(x, y, sx, sy, t, vx, vy, vt) {}

    double distance(double x, double y) const {
		x -= _posX;
		y -= _posY;
        rotate(x, y, -_theta);
		double dx = fabs(x) - _scaleX*0.5;
		double dy = fabs(y) - _scaleY*0.5;

		if (dx >= 0.0 || dy >= 0.0)
			return length(max(dx, 0.0), max(dy, 0.0));
		else
			return max(dx, dy);
    }
    
    void closestSurfacePoint(double &x, double &y) const {
		x -= _posX;
		y -= _posY;
		rotate(x, y, -_theta);
		double dx = fabs(x) - _scaleX*0.5;
		double dy = fabs(y) - _scaleY*0.5;

		if (dx > dy)
			x = nsgn(x)*0.5*_scaleX;
		else
			y = nsgn(y)*0.5*_scaleY;

		rotate(x, y, _theta);
		x += _posX;
		y += _posY;
    }
    
    void distanceNormal(double &nx, double &ny, double x, double y) const {
        x -= _posX;
        y -= _posY;
        rotate(x, y, -_theta);
        if (fabs(x) - _scaleX*0.5 > fabs(y) - _scaleY*0.5) {
            nx = nsgn(x);
            ny = 0.0;
        } else {
            nx = 0.0;
            ny = nsgn(y);
        }
        rotate(nx, ny, _theta);
    }
};

class SolidSphere: public SolidBody {
public:
    
    SolidSphere(double x, double y, double s, double t, double vx, double vy, double vt) :
        SolidBody(x, y, s, s, t, vx, vy, vt) {}
    
    double distance(double x, double y) const {
        return length(x - _posX, y - _posY) - _scaleX*0.5;
    }
    
    void closestSurfacePoint(double &x, double &y) const {
        globalToLocal(x, y);
        
		double r = length(x, y);
		if (r < 1e-4) {
			x = 0.5;
			y = 0.0;
		} else {
			x /= 2.0*r;
			y /= 2.0*r;
		}
        
		localToGlobal(x, y);
    }
    
    void distanceNormal(double &nx, double &ny, double x, double y) const {
        x -= _posX;
        y -= _posY;
        float r = (float)length(x, y);
        if (r < 1e-4) {
            nx = 1.0;
            ny = 0.0;
        } else {
            nx = x/r;
            ny = y/r;
        }
    }
};

class FluidQuantity {
    double *_src;
    double *_old; /* Contains old quantities at beginning of iteration */
    
    double *_phi;
    double *_volume;
    double *_normalX;
    double *_normalY;
    uint8_t *_cell;
    uint8_t *_body;
    uint8_t *_mask;

    int _w;
    int _h;
    double _ox;
    double _oy;
    double _hx;
    
    double lerp(double a, double b, double x) const {
        return a*(1.0 - x) + b*x;
    }
    
    /* Adds contribution `value' of sample at (x, y) to grid cell at (ix, iy)
     * using a hat filter.
     */
    void addSample(double *weight, double value, double x, double y, int ix, int iy) {
        if (ix < 0 || iy < 0 || ix >= _w || iy >= _h)
            return;
        
        double k = (1.0 - fabs(ix - x))*(1.0 - fabs(iy - y));
        weight[ix + iy*_w] += k;
        _src  [ix + iy*_w] += k*value;
    }
    
public:
    FluidQuantity(int w, int h, double ox, double oy, double hx)
            : _w(w), _h(h), _ox(ox), _oy(oy), _hx(hx) {
        _src = new double[_w*_h];
        _old = new double[_w*_h];
        
        _phi = new double[(_w + 1)*(_h + 1)];
        _volume  = new double[_w*_h];
        _normalX = new double[_w*_h];
        _normalY = new double[_w*_h];
        
        _cell = new uint8_t[_w*_h];
        _body = new uint8_t[_w*_h];
        _mask = new uint8_t[_w*_h];
        
        for (int i = 0; i < _w*_h; i++) {
            _cell[i] = CELL_FLUID;
            _volume[i] = 1.0;
        }
        
        memset(_src, 0, _w*_h*sizeof(double));
    }
    
    ~FluidQuantity() {
        delete[] _src;
        delete[] _old;

		delete [] _phi;
        delete [] _volume ;
        delete [] _normalX;
        delete [] _normalY;
        
        delete [] _cell;
        delete [] _body;
        delete [] _mask; 
    }
    
    double *src() {
        return _src;
    }
    
    const double *src() const {
        return _src;
    }
    
    const uint8_t *cell() const {
        return _cell;
    }
    
    const uint8_t *body() const {
        return _body;
    }
    
    int idx(int x, int y) const {
        return x + y*_w;
    }
    
    double at(int x, int y) const {
        return _src[x + y*_w];
    }
    
    double volume(int x, int y) const {
        return _volume[x + y*_w];
    }
    
    double &at(int x, int y) {
        return _src[x + y*_w];
    }
    
    void copy() {
        memcpy(_old, _src, _w*_h*sizeof(double));
    }
    
    /* Computes the change in quantity during the last update */
    void diff(double alpha) {
        for (int i = 0; i < _w*_h; i++)
            _src[i] -= (1.0 - alpha)*_old[i];
    }
    
    /* Reverses the previous transformation - saves memory */
    void undiff(double alpha) {
        for (int i = 0; i < _w*_h; i++)
            _src[i] += (1.0 - alpha)*_old[i];
    }
    
    double lerp(double x, double y) const {
        x = min(max(x - _ox, 0.0), _w - 1.001);
        y = min(max(y - _oy, 0.0), _h - 1.001);
        int ix = (int)x;
        int iy = (int)y;
        x -= ix;
        y -= iy;
        
        double x00 = at(ix + 0, iy + 0), x10 = at(ix + 1, iy + 0);
        double x01 = at(ix + 0, iy + 1), x11 = at(ix + 1, iy + 1);
        
        return lerp(lerp(x00, x10, x), lerp(x01, x11, x), y);
    }
    
    void addInflow(double x0, double y0, double x1, double y1, double v) {
        int ix0 = (int)(x0/_hx - _ox);
        int iy0 = (int)(y0/_hx - _oy);
        int ix1 = (int)(x1/_hx - _ox);
        int iy1 = (int)(y1/_hx - _oy);
        
        for (int y = max(iy0, 0); y < min(iy1, _h); y++) {
            for (int x = max(ix0, 0); x < min(ix1, _h); x++) {
                double l = length(
                    (2.0*(x + 0.5)*_hx - (x0 + x1))/(x1 - x0),
                    (2.0*(y + 0.5)*_hx - (y0 + y1))/(y1 - y0)
                );
                double vi = cubicPulse(l)*v;
                if (fabs(_src[x + y*_w]) < fabs(vi))
                    _src[x + y*_w] = vi;
            }
        }
    }
    
    void fillSolidFields(const vector<const SolidBody *> &bodies) {
        if (bodies.empty())
            return;
        
        for (int iy = 0, idx = 0; iy <= _h; iy++) {
            for (int ix = 0; ix <= _w; ix++, idx++) {
                double x = (ix + _ox - 0.5)*_hx;
                double y = (iy + _oy - 0.5)*_hx;
                
                _phi[idx] = bodies[0]->distance(x, y);
                for (unsigned i = 1; i < bodies.size(); i++)
                    _phi[idx] = min(_phi[idx], bodies[i]->distance(x, y));
            }
        }
        
        for (int iy = 0, idx = 0; iy < _h; iy++) {
            for (int ix = 0; ix < _w; ix++, idx++) {
                double x = (ix + _ox)*_hx;
                double y = (iy + _oy)*_hx;
                
                _body[idx] = 0;
                double d = bodies[0]->distance(x, y);
                for (unsigned i = 1; i < bodies.size(); i++) {
                    double id = bodies[i]->distance(x, y);
                    if (id < d) {
                        _body[idx] = i;
                        d = id;
                    }
                }
                
                int idxp = ix + iy*(_w + 1);
                _volume[idx] = 1.0 - occupancy(
                    _phi[idxp],          _phi[idxp + 1],
                    _phi[idxp + _w + 1], _phi[idxp + _w + 2]
                );
                
                if (_volume[idx] < 0.01)
                    _volume[idx] = 0.0;
                
                bodies[_body[idx]]->distanceNormal(_normalX[idx], _normalY[idx], x, y);
                
                if (_volume[idx] == 0.0)
                    _cell[idx] = CELL_SOLID;
                else
                    _cell[idx] = CELL_FLUID;
            }
        }
    }
    
    /* The extrapolation routine is now augmented to also fill in values for
     * cells that ended up with no particles in them. These are marked with
     * CELL_EMPTY. Empty cells are computed as the average value of all
     * available neighbours, and can therefore be computed as soon as at
     * least one neighbouring cell is available.
     */
    void fillSolidMask() {
        /* Make sure border is not touched by extrapolation - will be
         * handled separately.
         */
        for (int x = 0; x < _w; x++)
            _mask[x] = _mask[x + (_h - 1)*_w] = 0xFF;
        for (int y = 0; y < _h; y++)
            _mask[y*_w] = _mask[y*_w + _w - 1] = 0xFF;
        
        
        for (int y = 1; y < _h - 1; y++) {
            for (int x = 1; x < _w - 1; x++) {
                int idx = x + y*_w;

                _mask[idx] = 0;                
                if (_cell[idx] == CELL_SOLID) {
                    double nx = _normalX[idx];
                    double ny = _normalY[idx];

                    if (nx != 0.0 && _cell[idx + sgn(nx)]    != CELL_FLUID)
                        _mask[idx] |= 1;
                    if (ny != 0.0 && _cell[idx + sgn(ny)*_w] != CELL_FLUID)
                        _mask[idx] |= 2;
                } else if (_cell[idx] == CELL_EMPTY) {
                    /* Empty cells with no available neighbours need to be
                     * processed later.
                     */
                    _mask[idx] =
                        _cell[idx -  1] != CELL_FLUID &&
                        _cell[idx +  1] != CELL_FLUID &&
                        _cell[idx - _w] != CELL_FLUID &&
                        _cell[idx + _w] != CELL_FLUID;
                }
            }
        }
    }
    
    double extrapolateNormal(int idx) {
        double nx = _normalX[idx];
        double ny = _normalY[idx];
        
        double srcX = _src[idx + sgn(nx)];
        double srcY = _src[idx + sgn(ny)*_w];
        
        return (fabs(nx)*srcX + fabs(ny)*srcY)/(fabs(nx) + fabs(ny));
    }
    
    /* Computes the extrapolated value as the average of all available
     * neighbouring cells.
     */
    double extrapolateAverage(int idx) {
        double value = 0.0;
        int count = 0;
        
        if (_cell[idx - 1] == CELL_FLUID) {
            value += _src[idx - 1]; count++;
        }
        if (_cell[idx + 1] == CELL_FLUID) {
            value += _src[idx + 1]; count++;
        }
        if (_cell[idx - _w] == CELL_FLUID) {
            value += _src[idx - _w]; count++;
        }
        if (_cell[idx + _w] == CELL_FLUID) {
            value += _src[idx + _w]; count++;
        }
        return value/count;
    }
    
    void freeSolidNeighbour(int idx, stack<int> &border, int mask) {
        if (_cell[idx] == CELL_SOLID) {
            _mask[idx] &= ~mask;
            if (_mask[idx] == 0)
                border.push(idx);
        }
    }
    
    /* At least one free neighbour cell is enough to add this cell to the queue
     * of ready cells.
     */
    void freeEmptyNeighbour(int idx, stack<int> &border) {
        if (_cell[idx] == CELL_EMPTY) {
            if (_mask[idx] == 1) {
                _mask[idx] = 0;
                border.push(idx);
            }
        }
    }
    
    /* For empty cells on the border of the simulation domain, we simply copy
     * the values of the adjacent cells.
     */
    void extrapolateEmptyBorders() {
        for (int x = 1; x < _w - 1; x++) {
            int idxT = x;
            int idxB = x + (_h - 1)*_w;
            
            if (_cell[idxT] == CELL_EMPTY)
                _src[idxT] = _src[idxT + _w];
            if (_cell[idxB] == CELL_EMPTY)
                _src[idxB] = _src[idxB - _w];
        }
        
        for (int y = 1; y < _h - 1; y++) {
            int idxL = y*_w;
            int idxR = y*_w + _w - 1;
            
            if (_cell[idxL] == CELL_EMPTY)
                _src[idxL] = _src[idxL + 1];
            if (_cell[idxR] == CELL_EMPTY)
                _src[idxR] = _src[idxR - 1];
        }
        
        int idxTL = 0;
        int idxTR = _w - 1;
        int idxBL = (_h - 1)*_w;
        int idxBR = _h*_w - 1;
        
        /* Corner cells average the values of the two adjacent border cells */
        if (_cell[idxTL] == CELL_EMPTY)
            _src[idxTL] = 0.5*(_src[idxTL + 1] + _src[idxTL + _w]);
        if (_cell[idxTR] == CELL_EMPTY)
            _src[idxTR] = 0.5*(_src[idxTR - 1] + _src[idxTR + _w]);
        if (_cell[idxBL] == CELL_EMPTY)
            _src[idxBL] = 0.5*(_src[idxBL + 1] + _src[idxBL - _w]);
        if (_cell[idxBR] == CELL_EMPTY)
            _src[idxBR] = 0.5*(_src[idxBR - 1] + _src[idxBR - _w]);
        
        for (int i = 0; i < _w*_h; i++)
            if (_cell[i] == CELL_EMPTY)
                _cell[i] = CELL_FLUID;
    }
    
    void extrapolate() {
        fillSolidMask();

        stack<int> border;
        for (int y = 1; y < _h - 1; y++) {
            for (int x = 1; x < _w - 1; x++) {
                int idx = x + y*_w;

                if (_cell[idx] != CELL_FLUID && _mask[idx] == 0)
                    border.push(idx);
            }
        }

        while (!border.empty()) {
            int idx = border.top();
            border.pop();

            if (_cell[idx] == CELL_EMPTY) {
                _src[idx] = extrapolateAverage(idx);
                _cell[idx] = CELL_FLUID; /* Mark extrapolated empty cells as fluid */
            } else
                _src[idx] = extrapolateNormal(idx);

            if (_normalX[idx - 1] > 0.0)
                freeSolidNeighbour(idx -  1, border, 1);
            if (_normalX[idx + 1] < 0.0)
                freeSolidNeighbour(idx +  1, border, 1);
            if (_normalY[idx - _w] > 0.0)
                freeSolidNeighbour(idx - _w, border, 2);
            if (_normalY[idx + _w] < 0.0)
                freeSolidNeighbour(idx + _w, border, 2);
            
            /* Notify adjacent empty cells */
            freeEmptyNeighbour(idx -  1, border);
            freeEmptyNeighbour(idx +  1, border);
            freeEmptyNeighbour(idx - _w, border);
            freeEmptyNeighbour(idx + _w, border);
        }
        
        extrapolateEmptyBorders();
    }
    
    /* Transfers particle values onto grid using a linear filter.
     *
     * In a first step, particle values and filter weights are accumulated on
     * the grid by looping over all particles and adding the particle contribution
     * to the four closest grid cells.
     *
     * In a second step, the actual grid values are obtained by dividing by the
     * filter weights. Cells with weight zero are cells which do not contain any
     * particles and are subsequently marked as empty for extrapolation.
     */
    void fromParticles(double *weight, int count, double *posX, double *posY, double *property) {
        memset(_src,   0, _w*_h*sizeof(double));
        memset(weight, 0, _w*_h*sizeof(double));
        
        for (int i = 0; i < count; i++) {
            double x = posX[i] - _ox;
            double y = posY[i] - _oy;
            x = max(0.5, min(_w - 1.5, x));
            y = max(0.5, min(_h - 1.5, y));
            
            int ix = (int)x;
            int iy = (int)y;
            
            addSample(weight, property[i], x, y, ix + 0, iy + 0);
            addSample(weight, property[i], x, y, ix + 1, iy + 0);
            addSample(weight, property[i], x, y, ix + 0, iy + 1);
            addSample(weight, property[i], x, y, ix + 1, iy + 1);
        }
        
        for (int i = 0; i < _w*_h; i++) {
            if (weight[i] != 0.0)
                _src[i] /= weight[i];
            else if (_cell[i] == CELL_FLUID)
                _cell[i] = CELL_EMPTY;
        }
    }
};

/* Main class processing fluid particles */
class ParticleQuantities {
    /* Maximum allowed number of particles per cell */
    static const int _MaxPerCell = 12;
    /* Minimum allowed number of particles per cell */
    static const int _MinPerCell = 3;
    /* Initial number of particles per cell */
    static const int _AvgPerCell = 4;
    
    /* Number of particles currently active */
    int _particleCount;
    /* Maximum number of particles the simulation can handle */
    int _maxParticles;
    
    /* The usual culprits */
    int _w;
    int _h;
    double _hx;
    const vector<const SolidBody *> &_bodies;
    
    /* Filter weights (auxiliary array provided to fluid quantities) */
    double *_weight;
    /* Number of particles per cell */
    int *_counts;

    /* Particle positions */
    double *_posX;
    double *_posY;
    /* Particle 'properties', that is, value for each fluid quantity
     * (velocities, density etc.)
     */
    vector<double *> _properties;
    vector<FluidQuantity *> _quantities;
    
    /* Helper function returning true if a position is inside a solid body */
    bool pointInBody(double x, double y) {
        for (unsigned i = 0; i < _bodies.size(); i++)
            if (_bodies[i]->distance(x*_hx, y*_hx) < 0.0)
                return true;
        
        return false;
    }
    
    /* Initializes particle positions on randomly jittered grid locations */
    void initParticles() {
        int idx = 0;
        for (int y = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++) {
                for (int i = 0; i < _AvgPerCell; i++, idx++) {
                    _posX[idx] = x + frand();
                    _posY[idx] = y + frand();
                    
                    /* Discard particles landing inside solid bodies */
                    if (pointInBody(_posX[idx], _posY[idx]))
                        idx--;
                }
            }
        }
        
        _particleCount = idx;
    }
    
    /* Counts the number of particles per cell */
    void countParticles() {
        memset(_counts, 0, _w*_h*sizeof(int));
        for (int i = 0; i < _particleCount; i++) {
            int ix = (int)_posX[i];
            int iy = (int)_posY[i];
            
            if (ix >= 0 && iy >= 0 && ix < _w && iy < _h)
                _counts[ix + iy*_w]++;
        }
    }
    
    /* Decimates particles in crowded cells */
    void pruneParticles() {
        for (int i = 0; i < _particleCount; i++) {
            int ix = (int)_posX[i];
            int iy = (int)_posY[i];
            int idx = ix + iy*_w;
            
            if (ix < 0 && iy < 0 && ix >= _w && iy >= _h)
                continue;
            
            if (_counts[idx] > _MaxPerCell) {
                int j = --_particleCount;
                _posX[i] = _posX[j];
                _posY[i] = _posY[j];
                for (unsigned t = 0; t < _quantities.size(); t++)
                    _properties[t][i] = _properties[t][j];
                
                _counts[idx]--;
                i--;
            }
        }
    }
    
    /* Adds new particles in cells with dangerously little particles */
    void seedParticles() {
        for (int y = 0, idx = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++, idx++) {
                for (int i = 0; i < _MinPerCell - _counts[idx]; i++) {
                    if (_particleCount == _maxParticles)
                        return;
                    
                    int j = _particleCount;
                    
                    _posX[j] = x + frand();
                    _posY[j] = y + frand();
                    
                    /* Reject particle if it lands inside a solid body */
                    if (pointInBody(_posX[idx], _posY[idx]))
                        continue;
                    
                    /* Get current grid values */
                    for (unsigned t = 0; t < _quantities.size(); t++)
                        _properties[t][j] = _quantities[t]->lerp(_posX[j], _posY[j]);
                    
                    _particleCount++;
                }
            }
        }
    }
    
    /* Pushes particle back into the fluid if they land inside solid bodies */
    void backProject(double &x, double &y) {
        double d = 1e30;
        int closestBody = -1;
        for (unsigned i = 0; i < _bodies.size(); i++) {
            double id = _bodies[i]->distance(x*_hx, y*_hx);
            
            if (id < d) {
                d = id;
                closestBody = i;
            }
        }
        
        if (d < -1.0) {
            x *= _hx;
            y *= _hx;
            _bodies[closestBody]->closestSurfacePoint(x, y);
            double nx, ny;
            _bodies[closestBody]->distanceNormal(nx, ny, x, y);
            x -= nx*_hx;
            y -= ny*_hx;
            x /= _hx;
            y /= _hx;
        }
    }
    
    /* The same Runge Kutta interpolation routine as before - only now forward
     * in time instead of backwards.
     */
    void rungeKutta3(double &x, double &y, double timestep, const FluidQuantity &u, const FluidQuantity &v) const {
        double firstU = u.lerp(x, y)/_hx;
        double firstV = v.lerp(x, y)/_hx;

        double midX = x + 0.5*timestep*firstU;
        double midY = y + 0.5*timestep*firstV;

        double midU = u.lerp(midX, midY)/_hx;
        double midV = v.lerp(midX, midY)/_hx;

        double lastX = x + 0.75*timestep*midU;
        double lastY = y + 0.75*timestep*midV;

        double lastU = u.lerp(lastX, lastY);
        double lastV = v.lerp(lastX, lastY);
        
        x += timestep*((2.0/9.0)*firstU + (3.0/9.0)*midU + (4.0/9.0)*lastU);
        y += timestep*((2.0/9.0)*firstV + (3.0/9.0)*midV + (4.0/9.0)*lastV);
    }
    
public:
    ParticleQuantities(int w, int h, double hx,
            const vector<const SolidBody *> &bodies) :
            _w(w), _h(h), _hx(hx), _bodies(bodies) {
        
        _maxParticles = _w*_h*_MaxPerCell;
        
        _posX = new double[_maxParticles];
        _posY = new double[_maxParticles];
        
        _weight = new double[(_w + 1)*(_h + 1)];
        _counts = new int[_w*_h];
        
        initParticles();
    }
    
    /* Adds a new quantity to be carried by the particles */
    void addQuantity(FluidQuantity *q) {
        double *property = new double[_maxParticles];
        memset(property, 0, _maxParticles*sizeof(double));
        
        _quantities.push_back(q);
        _properties.push_back(property);
    }
    
    /* Interpolates the change in quantity back onto the particles.
     * Mixes in a little bit of the pure Particle-in-cell update using the
     * parameter alpha.
     */
    void gridToParticles(double alpha) {
        for (unsigned t = 0; t < _quantities.size(); t++) {
            for (int i = 0; i < _particleCount; i++) {
                _properties[t][i] *= 1.0 - alpha;
                _properties[t][i] += _quantities[t]->lerp(_posX[i], _posY[i]);
            }
        }
    }
    
    /* Interpolates particle quantities onto the grid, extrapolates them and
     * spawns/prunes particles where necessary.
     */
    void particlesToGrid() {
        for (unsigned t = 0; t < _quantities.size(); t++) {
            _quantities[t]->fromParticles(_weight, _particleCount, _posX, _posY, _properties[t]);
            _quantities[t]->extrapolate();
        }
        
        countParticles();
        pruneParticles();
        seedParticles();
        
        printf("Particle count: %d\n", _particleCount);
    }
    
    /* Advects particle in velocity field and clamps resulting positions to
     * the fluid domain */
    void advect(double timestep, const FluidQuantity &u, const FluidQuantity &v) {
        for (int i = 0; i < _particleCount; i++) {
            rungeKutta3(_posX[i], _posY[i], timestep, u, v);
            backProject(_posX[i], _posY[i]);
            
            _posX[i] = max(min(_posX[i], _w - 0.001), 0.0);
            _posY[i] = max(min(_posY[i], _h - 0.001), 0.0);
        }
    }
};

class FluidSolver {
    FluidQuantity *_d;
    FluidQuantity *_t;
    FluidQuantity *_u;
    FluidQuantity *_v;
    ParticleQuantities *_qs;
    
    double *_uDensity;
    double *_vDensity;
    
    int _w;
    int _h;
    
    double _hx;
    double _densityAir;
    double _densitySoot;
    double _diffusion;
    
    double *_r;
    double *_p;
    double *_z;
    double *_s;
    double *_precon;
    
    double *_aDiag;
    double *_aPlusX;
    double *_aPlusY;
    
    double _tAmb;
    double _g;
    /* Tiny blending factor for FLIP/PIC to avoid noise */
    double _flipAlpha;
    
    const vector<const SolidBody *> &_bodies;
    
    void buildRhs() {
        double scale = 1.0/_hx;
        const uint8_t *cell = _d->cell();
        const uint8_t *body = _d->body();
        
        for (int y = 0, idx = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++, idx++) {
                if (cell[idx] == CELL_FLUID) {
                    _r[idx] = -scale*
                        (_u->volume(x + 1, y)*_u->at(x + 1, y) - _u->volume(x, y)*_u->at(x, y) +
                         _v->volume(x, y + 1)*_v->at(x, y + 1) - _v->volume(x, y)*_v->at(x, y));
                    
                    double vol = _d->volume(x, y);
                    
                    if (_bodies.empty())
                        continue;
                    
                    if (x > 0)
                        _r[idx] -= (_u->volume(x, y) - vol)*_bodies[body[idx -  1]]->velocityX(x*_hx, (y + 0.5)*_hx);
                    if (y > 0)
                        _r[idx] -= (_v->volume(x, y) - vol)*_bodies[body[idx - _w]]->velocityY((x + 0.5)*_hx, y*_hx);
                    if (x < _w - 1)
                        _r[idx] += (_u->volume(x + 1, y) - vol)*_bodies[body[idx +  1]]->velocityX((x + 1.0)*_hx, (y + 0.5)*_hx);
                    if (y < _h - 1)
                        _r[idx] += (_v->volume(x, y + 1) - vol)*_bodies[body[idx + _w]]->velocityY((x + 0.5)*_hx, (y + 1.0)*_hx);
                } else
                    _r[idx] = 0.0;
            }
        }
    }
    
    void computeDensities() {
        double alpha = (_densitySoot - _densityAir)/_densityAir;
        
        memset(_uDensity, 0, (_w + 1)*_h*sizeof(double));
        memset(_vDensity, 0, _w*(_h + 1)*sizeof(double));
        
        for (int y = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++) {
                double density = _densityAir*_tAmb/_t->at(x, y)*(1.0 + alpha*_d->at(x, y));
                density = max(density, 0.05*_densityAir);
                
                _uDensity[_u->idx(x, y)]     += 0.5*density;
                _vDensity[_v->idx(x, y)]     += 0.5*density;
                _uDensity[_u->idx(x + 1, y)] += 0.5*density;
                _vDensity[_v->idx(x, y + 1)] += 0.5*density;
            }
        }
    }

    void buildPressureMatrix(double timestep) {
        double scale = timestep/(_hx*_hx);
        const uint8_t *cell = _d->cell();
        
        memset(_aDiag,  0, _w*_h*sizeof(double));
        memset(_aPlusX, 0, _w*_h*sizeof(double));
        memset(_aPlusY, 0, _w*_h*sizeof(double));

        for (int y = 0, idx = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++, idx++) {
                if (cell[idx] != CELL_FLUID)
                    continue;
                
                if (x < _w - 1 && cell[idx + 1] == CELL_FLUID) {
                    double factor = scale*_u->volume(x + 1, y)/_uDensity[_u->idx(x + 1, y)];
                    _aDiag [idx    ] +=  factor;
                    _aDiag [idx + 1] +=  factor;
                    _aPlusX[idx    ]  = -factor;
                }
                if (y < _h - 1 && cell[idx + _w] == CELL_FLUID) {
                    double factor = scale*_v->volume(x, y + 1)/_vDensity[_u->idx(x, y + 1)];
                    _aDiag [idx     ] +=  factor;
                    _aDiag [idx + _w] +=  factor;
                    _aPlusY[idx     ]  = -factor;
                }
            }
        }
    }
    
    void buildHeatDiffusionMatrix(double timestep) {
        for (int i = 0; i < _w*_h; i++)
            _aDiag[i] = 1.0;
        
        memset(_aPlusX, 0, _w*_h*sizeof(double));
        memset(_aPlusY, 0, _w*_h*sizeof(double));

        const uint8_t *cell = _d->cell();
        double scale = _diffusion*timestep*1.0/(_hx*_hx);

        for (int y = 0, idx = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++, idx++) {
                if (cell[idx] != CELL_FLUID)
                    continue;
                
                if (x < _w - 1 && cell[idx + 1] == CELL_FLUID) {
                    _aDiag [idx    ] +=  scale;
                    _aDiag [idx + 1] +=  scale;
                    _aPlusX[idx    ]  = -scale;
                }

                if (y < _h - 1 && cell[idx + _w] == CELL_FLUID) {
                    _aDiag [idx     ] +=  scale;
                    _aDiag [idx + _w] +=  scale;
                    _aPlusY[idx     ]  = -scale;
                }
            }
        }
    }
    
    void buildPreconditioner() {
        const double tau = 0.97;
        const double sigma = 0.25;
        const uint8_t *cell = _d->cell();

        for (int y = 0, idx = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++, idx++) {
                if (cell[idx] != CELL_FLUID)
                    continue;
                
                double e = _aDiag[idx];

                if (x > 0 && cell[idx - 1] == CELL_FLUID) {
                    double px = _aPlusX[idx - 1]*_precon[idx - 1];
                    double py = _aPlusY[idx - 1]*_precon[idx - 1];
                    e = e - (px*px + tau*px*py);
                }
                if (y > 0 && cell[idx - _w] == CELL_FLUID) {
                    double px = _aPlusX[idx - _w]*_precon[idx - _w];
                    double py = _aPlusY[idx - _w]*_precon[idx - _w];
                    e = e - (py*py + tau*px*py);
                }

                if (e < sigma*_aDiag[idx])
                    e = _aDiag[idx];

                _precon[idx] = 1.0/sqrt(e);
            }
        }
    }
    
    void applyPreconditioner(double *dst, double *a) {
        const uint8_t *cell = _d->cell();
        
        for (int y = 0, idx = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++, idx++) {
                if (cell[idx] != CELL_FLUID)
                    continue;
                
                double t = a[idx];

                if (x > 0 && cell[idx -  1] == CELL_FLUID)
                    t -= _aPlusX[idx -  1]*_precon[idx -  1]*dst[idx -  1];
                if (y > 0 && cell[idx - _w] == CELL_FLUID)
                    t -= _aPlusY[idx - _w]*_precon[idx - _w]*dst[idx - _w];

                dst[idx] = t*_precon[idx];
            }
        }

        for (int y = _h - 1, idx = _w*_h - 1; y >= 0; y--) {
            for (int x = _w - 1; x >= 0; x--, idx--) {
                if (cell[idx] != CELL_FLUID)
                    continue;
                
                double t = dst[idx];

                if (x < _w - 1 && cell[idx +  1] == CELL_FLUID)
                    t -= _aPlusX[idx]*_precon[idx]*dst[idx +  1];
                if (y < _h - 1 && cell[idx + _w] == CELL_FLUID)
                    t -= _aPlusY[idx]*_precon[idx]*dst[idx + _w];

                dst[idx] = t*_precon[idx];
            }
        }
    }
    
    double dotProduct(double *a, double *b) {
        const uint8_t *cell = _d->cell();
        
        double result = 0.0;
        for (int i = 0; i < _w*_h; i++)
            if (cell[i] == CELL_FLUID)
                result += a[i]*b[i];
        return result;
    }
    
    void matrixVectorProduct(double *dst, double *b) {
        for (int y = 0, idx = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++, idx++) {
                double t = _aDiag[idx]*b[idx];
                
                if (x > 0)
                    t += _aPlusX[idx -  1]*b[idx -  1];
                if (y > 0)
                    t += _aPlusY[idx - _w]*b[idx - _w];
                if (x < _w - 1)
                    t += _aPlusX[idx]*b[idx +  1];
                if (y < _h - 1)
                    t += _aPlusY[idx]*b[idx + _w];

                dst[idx] = t;
            }
        }
    }
    
    void scaledAdd(double *dst, double *a, double *b, double s) {
        const uint8_t *cell = _d->cell();
        
        for (int i = 0; i < _w*_h; i++)
            if (cell[i] == CELL_FLUID)
                dst[i] = a[i] + b[i]*s;
    }
    
    double infinityNorm(double *a) {
        const uint8_t *cell = _d->cell();
        
        double maxA = 0.0;
        for (int i = 0; i < _w*_h; i++)
            if (cell[i] == CELL_FLUID)
                maxA = max(maxA, fabs(a[i]));
        return maxA;
    }
    
    void project(int limit) {
        memset(_p, 0,  _w*_h*sizeof(double));
        applyPreconditioner(_z, _r);
        memcpy(_s, _z, _w*_h*sizeof(double));
        
        double maxError = infinityNorm(_r);
        if (maxError < 1e-5) {
            printf("Initial guess sufficiently small\n");
            return;
        }
        
        double sigma = dotProduct(_z, _r);
        
        for (int iter = 0; iter < limit; iter++) {
            matrixVectorProduct(_z, _s);
            double alpha = sigma/dotProduct(_z, _s);
            scaledAdd(_p, _p, _s, alpha);
            scaledAdd(_r, _r, _z, -alpha);
            
            maxError = infinityNorm(_r);
            if (maxError < 1e-5) {
                printf("Exiting solver after %d iterations, maximum error is %f\n", iter, maxError);
                return;
            }
            
            applyPreconditioner(_z, _r);
            
            double sigmaNew = dotProduct(_z, _r);
            scaledAdd(_s, _z, _s, sigmaNew/sigma);
            sigma = sigmaNew;
        }
        
        printf("Exceeded budget of %d iterations, maximum error was %f\n", limit, maxError);
    }
    
    void applyPressure(double timestep) {
        double scale = timestep/_hx;
        const uint8_t *cell = _d->cell();
        
        for (int y = 0, idx = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++, idx++) {
                if (cell[idx] != CELL_FLUID)
                    continue;
                
                _u->at(x, y)     -= scale*_p[idx]/_uDensity[_u->idx(x, y)];
                _v->at(x, y)     -= scale*_p[idx]/_vDensity[_v->idx(x, y)];
                _u->at(x + 1, y) += scale*_p[idx]/_uDensity[_u->idx(x + 1, y)];
                _v->at(x, y + 1) += scale*_p[idx]/_vDensity[_v->idx(x, y + 1)];
            }
        }
    }
    
    void addBuoyancy(double timestep) {
        double alpha = (_densitySoot - _densityAir)/_densityAir;

        for (int y = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++) {
                double buoyancy = timestep*_g*(alpha*_d->at(x, y) - (_t->at(x, y) - _tAmb)/_tAmb);

                _v->at(x, y    ) += buoyancy*0.5;
                _v->at(x, y + 1) += buoyancy*0.5;
            }
        }
    }
    
    void setBoundaryCondition() {
        const uint8_t *cell = _d->cell();
        const uint8_t *body = _d->body();
        
        for (int y = 0, idx = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++, idx++) {
                if (cell[idx] == CELL_SOLID) {
                    const SolidBody &b = *_bodies[body[idx]];
                    
                    _u->at(x, y) = b.velocityX(x*_hx, (y + 0.5)*_hx);
                    _v->at(x, y) = b.velocityX((x + 0.5)*_hx, y*_hx);
                    _u->at(x + 1, y) = b.velocityX((x + 1.0)*_hx, (y + 0.5)*_hx);
                    _v->at(x, y + 1) = b.velocityX((x + 0.5)*_hx, (y + 1.0)*_hx);
                }
            }
        }
        
        for (int y = 0; y < _h; y++)
            _u->at(0, y) = _u->at(_w, y) = 0.0;
        for (int x = 0; x < _w; x++)
            _v->at(x, 0) = _v->at(x, _h) = 0.0;
    }
    
public:
    FluidSolver(int w, int h, double rhoAir, double rhoSoot, double diffusion,
            const vector<const SolidBody *> &bodies) : _w(w), _h(h),
            _densityAir(rhoAir), _densitySoot(rhoSoot), _diffusion(diffusion),
            _bodies(bodies) {
                
        _tAmb      = 294.0;
        _g         = 9.81;
        _flipAlpha = 0.001;
                
        _hx = 1.0/min(w, h);
        
        _d = new FluidQuantity(_w,     _h,     0.5, 0.5, _hx);
        _t = new FluidQuantity(_w,     _h,     0.5, 0.5, _hx);
        _u = new FluidQuantity(_w + 1, _h,     0.0, 0.5, _hx);
        _v = new FluidQuantity(_w,     _h + 1, 0.5, 0.0, _hx);
                
        for (int i = 0; i < _w*_h; i++)
            _t->src()[i] = _tAmb;
                
        _qs = new ParticleQuantities(_w, _h, _hx, _bodies);
        _qs->addQuantity(_d);
        _qs->addQuantity(_t);
        _qs->addQuantity(_u);
        _qs->addQuantity(_v);
        /* Interpolate initial quantity distribution onto particles */
        _qs->gridToParticles(1.0);
        
        _r = new double[_w*_h];
        _p = new double[_w*_h];
        _z = new double[_w*_h];
        _s = new double[_w*_h];
        _aDiag  = new double[_w*_h];
        _aPlusX = new double[_w*_h];
        _aPlusY = new double[_w*_h];
        _precon = new double[_w*_h];
                
        _uDensity = new double[(_w + 1)*_h];
        _vDensity = new double[_w*(_h + 1)];
    }
       //added by Mobeen
	~FluidSolver() {
		delete _d;
		delete _u;
		delete _v;

		delete [] _r;
		delete [] _p;
		delete [] _z;
		delete [] _s;
		delete [] _aDiag;
		delete [] _aPlusX;
		delete [] _aPlusY;
		delete [] _precon;

		delete [] _uDensity;
		delete [] _vDensity;
	}

    void update(double timestep) {
        _d->fillSolidFields(_bodies);
        _t->fillSolidFields(_bodies);
        _u->fillSolidFields(_bodies);
        _v->fillSolidFields(_bodies);
        
        /* Interpolate particle quantities to grid */
        _qs->particlesToGrid();
        
        /* Set current values as the old/pre-update values */
        _d->copy();
        _t->copy();
        _u->copy();
        _v->copy();
        
        /* Unfortunately, we have to move inflows out of the mainloop into here
         * - all changes need to happen between copy and diff to have any effect
         */
        addInflow(0.45, 0.2, 0.2, 0.05, 1.0, _tAmb, 0.0, 0.0);
        
        memcpy(_r, _t->src(), _w*_h*sizeof(double));
        buildHeatDiffusionMatrix(timestep);
        buildPreconditioner();
        project(2000);
        memcpy(_t->src(), _p, _w*_h*sizeof(double));
        
        _t->extrapolate();
        
        addBuoyancy(timestep);
        setBoundaryCondition();
        
        buildRhs();
        computeDensities();
        buildPressureMatrix(timestep);
        buildPreconditioner();
        project(2000);
        applyPressure(timestep);
        
        _d->extrapolate();
        _u->extrapolate();
        _v->extrapolate();
        
        setBoundaryCondition();
        
        /* Compute change in quantities */
        _d->diff(_flipAlpha);
        _t->diff(_flipAlpha);
        _u->diff(_flipAlpha);
        _v->diff(_flipAlpha);
        
        /* Interpolate change onto particles */
        _qs->gridToParticles(_flipAlpha);

        /* Reverse the change computation to get the post-update values back
         * (for rendering/advection).
         */
        _d->undiff(_flipAlpha);
        _t->undiff(_flipAlpha);
        _u->undiff(_flipAlpha);
        _v->undiff(_flipAlpha);
        
        /* Advect particles in velocity field */
        _qs->advect(timestep, *_u, *_v);
    }
    
    void addInflow(double x, double y, double w, double h, double d, double t, double u, double v) {
        _d->addInflow(x, y, x + w, y + h, d);
        _t->addInflow(x, y, x + w, y + h, t);
        _u->addInflow(x, y, x + w, y + h, u);
        _v->addInflow(x, y, x + w, y + h, v);
    }
    
    double ambientT() {
        return _tAmb;
    }
    
    void toImage(unsigned char *rgba, bool renderHeat) {
        for (int y = 0; y < _h; y++) {
            for (int x = 0; x < _w; x++) {
                int idxl, idxr;
                if (renderHeat) {
                    idxl = 4*(x + y*_w*2);
                    idxr = 4*(x + y*_w*2 + _w);
                } else
                    idxr = 4*(x + y*_w);
                
                double volume = _d->volume(x, y);
                
                double shade = (1.0 - _d->at(x, y))*volume;
                shade = min(max(shade, 0.0), 1.0);
                rgba[idxr + 0] = (int)(shade*255.0);
                rgba[idxr + 1] = (int)(shade*255.0);
                rgba[idxr + 2] = (int)(shade*255.0);
                rgba[idxr + 3] = 0xFF;
                
                if (_d->cell()[x + y*_w] == CELL_EMPTY) {
                    rgba[idxr] = 0xFF;
                    rgba[idxr + 1] = rgba[idxr + 2] = 0;
                }
                
                if (renderHeat) {
                    double t = fabs(_t->at(x, y) - _tAmb)/70.0;
                    
                    t = min(max(t, 0.0), 1.0);
                    
                    double r = 1.0 + volume*(min(t*4.0, 1.0) - 1.0);
                    double g = 1.0 + volume*(min(t*2.0, 1.0) - 1.0);
                    double b = 1.0 + volume*(max(min(t*4.0 - 3.0, 1.0), 0.0) - 1.0);
                    
                    rgba[idxl + 0] = (int)(r*255.0);
                    rgba[idxl + 1] = (int)(g*255.0);
                    rgba[idxl + 2] = (int)(b*255.0);
                    rgba[idxl + 3] = 0xFF;
                }
            }
        }
    }
};

/* Play with these constants, if you want */
const int sizeX = 128;
const int sizeY = 128;

const double densityAir = 0.1;
const double densitySoot = 0.25; /* You can make this smaller to get lighter smoke */
const double diffusion = 0.01;
const double timestep = 0.0025;

const bool renderHeat = false; /* Set this to true to enable heat rendering */
 

const int	WINDOW_WIDTH=512, WINDOW_HEIGHT=512; 
int		startTime=0;
int		totalFrames=0;
float	fps=0;
char	buffer[MAX_PATH];
  
unsigned char *image;

vector<SolidBody *> bodies;
vector<const SolidBody *> cBodies;

FluidSolver *pSolver;
GLuint textureID;

void OnInit() {
	
    bodies.push_back(new SolidBox(0.5, 0.6, 0.7, 0.1, M_PI*0.25, 0.0, 0.0, 0.0));
    
    for (unsigned i = 0; i < bodies.size(); i++)
        cBodies.push_back(bodies[i]);

    pSolver = new FluidSolver(sizeX, sizeY, densityAir, densitySoot, diffusion, cBodies);


	if(renderHeat)
		image = new unsigned char[sizeX*2*sizeY*4];
	else
		image = new unsigned char[sizeX*sizeY*4];

	//create GL Texture
	glEnable(GL_TEXTURE_2D);
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); 
	if(renderHeat)
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sizeX*2, sizeY, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
	else
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sizeX, sizeY, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
}

void OnShutdown() { 
	delete pSolver;
	delete [] image;
	glDeleteTextures(1, &textureID);
	const int total = bodies.size();
	for(int i=0;i<total;++i) {
		delete bodies[i];
		cBodies[i] = nullptr;
	}
}

void DrawFluid() {
	//glDrawPixels(sizeX, sizeY, GL_RGBA, GL_UNSIGNED_BYTE, image);

	glBegin(GL_QUADS);
		glTexCoord2f(0,0);	glVertex2f(-1,-1);
		glTexCoord2f(1,0);	glVertex2f(1,-1);
		glTexCoord2f(1,1);	glVertex2f(1,1);
		glTexCoord2f(0,1);	glVertex2f(-1,1);
	glEnd();
}

void OnRender() { 
	//Calculate fps
	totalFrames++;
	int current = glutGet(GLUT_ELAPSED_TIME);
	if((current-startTime)>1000)
	{		
		float elapsedTime = float(current-startTime);
		fps = ((totalFrames * 1000.0f)/ elapsedTime) ;		 
		sprintf_s(buffer, "FPS: %3.2f",fps);
		glutSetWindowTitle(buffer);
		startTime = current;
		totalFrames=0;
	}

	glClear(GL_COLOR_BUFFER_BIT);
	glLoadIdentity();
	 
	DrawFluid(); 
	 
	glutSwapBuffers();
}


void OnIdle() {	   
	for(int i=0;i<4;++i) {
		pSolver->addInflow(0.35, 0.9, 0.1, 0.05, 1.0, pSolver->ambientT() + 300.0, 0.0, 0.0);
		pSolver->update(timestep);	
	}
	pSolver->toImage(image, renderHeat);
	if(renderHeat)
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sizeX*2, sizeY, GL_RGBA, GL_UNSIGNED_BYTE, image);
	else
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sizeX, sizeY, GL_RGBA, GL_UNSIGNED_BYTE, image);
	glutPostRedisplay();
	for (unsigned i = 0; i < bodies.size(); i++)
		bodies[i]->update(timestep);
}

int main(int argc, char** argv) {
	
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
	glutInitWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);
	glutCreateWindow("GLUT Fluids Demo [CPU]"); 

	glutCloseFunc(OnShutdown);
	glutDisplayFunc(OnRender); 
	glutIdleFunc(OnIdle);

	OnInit(); 

	glutMainLoop();		
    
    
	/*
    double time = 0.0;
    int iterations = 0;
    
    while (time < 8.0) {
        for (int i = 0; i < 4; i++) {
            solver->update(timestep);
            time += timestep;
            fflush(stdout);
        }
        
        solver->toImage(image, renderHeat);
        
        char path[256];
        sprintf(path, "Frame%05d.png", iterations++);
        lodepng_encode32_file(path, image, (renderHeat ? sizeX*2 : sizeX), sizeY);
        
        for (unsigned i = 0; i < bodies.size(); i++)
            bodies[i]->update(timestep);
    }
	*/

    return 0;
}
