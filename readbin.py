from struct import unpack, calcsize
from sys import argv
from os.path import splitext
from numpy import zeros, complex_

def readbin(input_files, save=False):
    noargs = len(input_files)

    if ( noargs == 0 ):
        print( "No filename specified." )
        exit()

    fh = open(input_files, 'rb' )
    print("Read file: "+input_files)
    header_raw = fh.read(calcsize("llllllliidddddddddddddd"))
    header = unpack( "llllllliidddddddddddddd", header_raw )
    nDims = header[3]
    nDimX = header[4]
    nDimY = header[5]
    nDimZ = header[6]
    bCmpx = header[8]
    t     = header[9]
    xMin  = header[10]
    xMax  = header[11]
    yMin  = header[12]
    yMax  = header[13]
    zMin  = header[14]
    zMax  = header[15]
    dx    = header[16]
    dy    = header[17]
    dz    = header[18]
    dkx   = header[19]
    dky   = header[20]
    dkz   = header[21]
    dt    = header[22]

    if ( header[0] != 1380 ):
        print( "Invalid file format." )
        exit()

    if ( bCmpx != 1 ):
        print( "File does not contain complex data." )
        exit()


    newfilename = splitext( input_files )[0] + ".mat"

    fh.seek(header[0],0)

    cmplxsize = calcsize("dd")

    if (nDims == 1):
        data = zeros((nDimX),dtype=complex_) 
        for i in range(0, nDimX):
            rawcplxno = fh.read(cmplxsize)
            cmplxno = unpack( "dd", rawcplxno )
            data[i] = complex(cmplxno[0],cmplxno[1])
        if save == True:
            print("dims = (%ld,%ld,%ld)" % (nDimX,nDimY,nDimZ))
            print("xrange = (%g,%g)" % (xMin,xMax))
            print("t = %g" % (t))
            import scipy.io as sio
            sio.savemat(newfilename, mdict={'wavefunction': data, 'nDimX': nDimX, 'xMin': xMin, 'xMax': xMax, 'dx': dx, 't': t} )
            print(newfilename+" created.")
        return {'wavefunction': data, 'nDimX': nDimX, 'xMin': xMin, 'xMax': xMax, 'dx': dx, 't': t}

    if (nDims == 2):
        data = zeros((nDimX,nDimY),dtype=complex_) 
        for i in range(0, nDimX):
            for j in range(0, nDimY):
                rawcplxno = fh.read(cmplxsize)
                cmplxno = unpack( "dd", rawcplxno )
                data[j,i] = complex(cmplxno[0],cmplxno[1])
        if save == True:
            print("dims = (%ld,%ld,%ld)" % (nDimX,nDimY,nDimZ))
            print("xrange = (%g,%g)" % (xMin,xMax))
            print("yrange = (%g,%g)" % (yMin,yMax))
            print("t = %g" % (t))
            import scipy.io as sio
            sio.savemat(newfilename, mdict={'wavefunction': data, 'nDimX': nDimX, 'nDimY': nDimY, 'xMin': xMin, 'yMin': yMin, 'xMax': xMax, 'yMax': yMax, 'dx': dx, 'dy': dy, 't': t} )
            print(newfilename+" created.")
        return {'wavefunction': data, 'nDimX': nDimX, 'nDimY': nDimY, 'xMin': xMin, 'yMin': yMin, 'xMax': xMax, 'yMax': yMax, 'dx': dx, 'dy': dy, 't': t}
            
        
    fh.close()

if __name__ == '__main__':
    for i in range(1, len(argv)):
        readbin(argv[i], save=True)