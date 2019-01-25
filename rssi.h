#ifndef _RSSI_H
#define _RSSI_H

typedef std::function<bool(int)> updater;

class RSSI {
public:
	RSSI(TFT_eSPI &tft, int n, int f, int b): _tft(tft), _n(n), _f(f), _b(b) {}

	void init(int x, int y, int w, int h) {
		_x = x;
		_y = y;
		_dx = w / _n;
		_dy = h / _n;
	}

	void update(updater u) {
		int h = _dy;
		int x = _x, y = _y + _n*_dy;
		for (int i = 0; i < _n; i++) {
			if (u(i))
				_tft.fillRect(x, y, _dx, h, _f);
			else {
				_tft.fillRect(x, y, _dx, h, _b);
				_tft.drawRect(x, y, _dx, h, _f);
			}
			x += _dx;
			y -= _dy;
			h += _dy;
		}
	}

private:
	int _n, _x, _y, _dx, _dy, _f, _b;
	TFT_eSPI &_tft;

};

#endif
