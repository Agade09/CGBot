all:
	g++-8 CGBot.cpp -o CGBot -std=c++17 -O3 -march=native -lgloox -lpthread -lstdc++fs
