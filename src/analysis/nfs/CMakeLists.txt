#
# (c) Copyright 2008, Hewlett-Packard Development Company, LP
#
#  See the file named COPYING for license details
#
# cmake rules for the src/analysis/nfs directory

DATASERIES_PROGRAM(nfssubset)

IF(CRYPTO_ENABLED)
    DATASERIES_PROGRAM(nfsdsanalysis 
    	common.cpp mod1.cpp mod2.cpp mod3.cpp mod4.cpp 
    	HostInfo.cpp 
    	MergeJoins.cpp
    	MissingOps.cpp
    	ReadWriteExtent.cpp
    	ServerLatency.cpp 
    	Sequentiality.cpp
    	UniqueFileHandles.cpp
        ../../process/sourcebyrange.cpp)
ENDIF(CRYPTO_ENABLED)

DATASERIES_PROGRAM(ipnfscrosscheck)
