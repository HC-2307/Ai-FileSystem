#include<fstream>
#include <vector>
using namespace std;


class block_storage{
private:
    fstream file;
    string filename;

    static const int BLOCK_SIZE = 4096;
    int blockCount;

public:
    void create(string filename, int blockCount){

    }

    void open(string filename){

    }

    void writeBlock(int blockNumber, vector<char> data){

    }

    vector<char> readBlock(int blockNumber){

    }


};