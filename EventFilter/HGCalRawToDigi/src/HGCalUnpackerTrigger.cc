#include "EventFilter/HGCalRawToDigi/interface/HGCalUnpackerTrigger.h"
#include "EventFilter/HGCalRawToDigi/interface/TPG/TPGFEDataformat.hh"
#include "EventFilter/HGCalRawToDigi/interface/TPG/TPGBEDataformat.hh"
#include "EventFilter/HGCalRawToDigi/interface/TPG/Stage1IO.hh"
#include "EventFilter/HGCalRawToDigi/interface/TPG/TpgSubpacketHeader.h"

using namespace hgcal;

bool HGCalUnpackerTrigger::parseFEDData(unsigned fedId,
                                        const RawFragmentWrapper& fed_data,
                                        const HGCalTriggerConfiguration& config,
                                        const HGCalMappingModuleIndexerTrigger& moduleIndexer,
                                        hgcaldigi::HGCalDigiTriggerHost& digisTrigger) {
  
  // Endianness assumption
  // From 32-bit word(ECOND) to 64-bit word(capture block): little endianness
  // Others: big endianness
  
  // TODO: if this also depends on the unpacking configuration, it should be moved to the specialization
  //const auto& fedConfig = config.feds[fedId];
  const auto& fedReadoutSequence = moduleIndexer.fedReadoutSequences()[fedId];
  const auto* start_fed_data = &(fed_data.data().front());
  const auto* const header = reinterpret_cast<const uint64_t*>(start_fed_data);
  const auto* const trailer = reinterpret_cast<const uint64_t*>(start_fed_data + fed_data.size());
  
  LogDebug("[HGCalUnpackerTrigger]") << " nwords (64b) = " << std::distance(header, trailer) << "\n";

  
  const uint64_t* ptr = header;
  char num[10], word64[20], word32m[20], word32l[20];
  for (unsigned iword = 0; ptr < trailer; ++iword) {
    uint64_t tword = *ptr;
    uint32_t tword32m = ((tword>>32) & 0xffffffff);
    uint32_t tword32l = tword & 0xffffffff;
    sprintf(num,"%03u",iword);
    sprintf(word64,"0x%016lx",tword);
    sprintf(word32m,"0x%08x",tword32m);
    sprintf(word32l,"0x%08x",tword32l);

    LogDebug("[HGCalUnpackerTrigger]")  << "HGCalUnpackerTrigger::parseFEDData::tword " << num << " " << word64  << " (" << word32m << ", " << word32l << ")" << std::endl;
    ++ptr;
  }
  
  unsigned n64(std::distance(header, trailer));  
  const Hgcal10gLinkReceiver::TpgSubpacketHeader *tsh(reinterpret_cast<const Hgcal10gLinkReceiver::TpgSubpacketHeader*>(header+2));
  const Hgcal10gLinkReceiver::TpgSubpacketHeader *tshEnd(reinterpret_cast<const Hgcal10gLinkReceiver::TpgSubpacketHeader*>(header+n64-2-2));
  
  if(!tsh->validPattern()) {
    tsh->print();
    return false;
  }

  tsh=tsh->nextSubpacketHeader();
  int noffecafe = 0;
  bool done(false);
  uint32_t econTOffset = 0; ///THIS DEPENDS ON module
  uint32_t denseIndexOffset = 0 ;
  while(tsh<=tshEnd && !done) {
    if(!tsh->validPattern()) {
      done=true;	
    } else {
      if((tsh->channelId()%2)==0) { // RX only
	//tsh->print();	  
	unsigned emp_chan(tsh->channelId()/2);
	//// WE NEED **CONFIGURE** THE MODULES TO BE READ, 100 stands for the first module
	//if(emp_chan==100 or emp_chan==102 or emp_chan==104 or emp_chan==108){
	if(emp_chan==100 or emp_chan==102){
	  //// WE NEED TO **CONFIGURE** THE Number of ECONT-s connected to this emp_channel and then nof elinks associated with each ECON-T
	  uint32_t nEconTs = 1 ; //// A test setting but needs to be **CONFIGURE** ed from json
	  for(unsigned bx(0);bx<tsh->numberOfBxs();bx++) {
	    const uint64_t *el64packed((const uint64_t*)(tsh+1+bx*tsh->numberOfWordsPerBx()));
	    uint32_t *elinks = new uint32_t[tsh->numberOfWordsPerBx()];
	    for(unsigned j(0);j<tsh->numberOfWordsPerBx();j++) {
	      elinks[2*j] = el64packed[j] & 0xffffffff;
	      elinks[2*j+1] = (el64packed[j]>>32) & 0xffffffff;
	      sprintf(word64,"0x%016lx",el64packed[j]);
	      LogDebug("[HGCalUnpackerTrigger]")  << "Word " << std::setw(6) << j << " = 0x"
						  << std::hex << std::setfill('0')
						  << std::setw(16) << word64
						  << std::dec << std::setfill(' ')
						  << std::endl;	      
	      
	    }
	    for(unsigned iel(0);iel<8;iel++) {
	      sprintf(word32m,"0x%08x",elinks[iel]);
	      LogDebug("[HGCalUnpackerTrigger]")  << "\t elink " << std::setw(3) << iel << " = 0x"
						  << std::hex << std::setfill('0')
						  << std::setw(8) << word32m
						  << std::dec << std::setfill(' ')
						  << std::endl;	      
	    }

	    //// WE NEED TO **CONFIGURE** THE Channel number for Si and Scitillators
	    if(emp_chan!=123){
	      // /////////////////////////// Si ////////////////////////////
	      uint32_t nprevTxs = 0 ;
	      for(unsigned iecon(0) ; iecon < nEconTs ; iecon++) {
		//// WE NEED TO **CONFIGURE** the nof elinks associated with each ECON-T
		const int neTx = 4;
		uint32_t *el = new uint32_t[neTx];
		TPGFEConfiguration::ConfigEconT cfgecont;
		cfgecont.setNElinks(uint32_t(neTx));
		// // WE NEED TO **CONFIGURE** ECONT mode
		cfgecont.setSelect(2); //2 for bast choice

		/*//-----------------------------*/
		//Run 110693
		// el[0] = elinks[nprevTxs+2];
		// el[1] = elinks[nprevTxs+1];
		// el[2] = elinks[nprevTxs+0];
		// el[3] = elinks[nprevTxs+3];
		/*///-----------------------------*/
		
		/*//-----------------------------*/
		//Run 111137 and 111138 for later runs use the one below
		//for(int iel=0;iel<neTx;iel++) el[iel] = elinks[nprevTxs + (6-iel)];
		/*///-----------------------------*/
		
		/*//-----------------------------*/
		//Runs >= 111139
		for(int iel=0;iel<neTx;iel++) el[iel] = elinks[nprevTxs + iel];
		/*///-----------------------------*/
		
		TPGFEDataformat::TcRawDataPacket rdp;
		TPGStage1Emulation::Stage1IO::convertElinksToTcRawData(cfgecont.getOutType(), cfgecont.getNofTCs(), el, rdp);		
		//rdp.print();
		
		delete [] el;
		
		uint32_t totE = 0;
		for(const auto& itc: rdp.getTcData()) totE += itc.decodedE(rdp.type());

		//// How much of below will be **CONFIGURE** ed
		uint32_t econTId = iecon + econTOffset; //unique per fedId
		for(unsigned itc(0) ; itc < rdp.size() ; itc++){

		  uint32_t tcidx = uint32_t(rdp.getTc(itc).address()); 
		  // uint32_t denseIdx = tcidx + fedReadoutSequence.TCOffsets_.at(econTId) ; //same as following function call
		  uint32_t denseIdx = moduleIndexer.getIndexForModuleData(fedId, econTId, tcidx) ;
		  
		  digisTrigger.view()[denseIdx].algo() = uint8_t(cfgecont.getOutType());
		  digisTrigger.view()[denseIdx].valid()(bx,0) = true;
		  digisTrigger.view()[denseIdx].nBxs() = uint8_t(tsh->numberOfBxs());
		  digisTrigger.view()[denseIdx].econTId() = econTId;
		  digisTrigger.view()[denseIdx].nTCs() = uint8_t(cfgecont.getNofTCs());
		  digisTrigger.view()[denseIdx].bxId()(bx,0) = uint8_t(rdp.bx());
		  digisTrigger.view()[denseIdx].TotE()(bx,0) = (rdp.type()==TPGFEDataformat::BestC)? uint32_t(TPGFEDataformat::TcRawData::Decode5E3M(rdp.moduleSum())) : totE ;
		  digisTrigger.view()[denseIdx].TCEnergy()(bx,0) = uint32_t(rdp.getTc(itc).decodedE(rdp.type()));
		  digisTrigger.view()[denseIdx].TCAddress()(bx,0) = uint8_t(rdp.getTc(itc).address());

		  LogDebug("[HGCalUnpackerTrigger]")  << "HGCalUnpackerTrigger::parseFEDData fedId : " << fedId
			     << ", iecon " << iecon
			     << ", econTId " << econTId
			     << ", tcidx: " << tcidx
			     << ", denseIdx: " << denseIdx
			     << ", getDenseTCIndex00: " << moduleIndexer.getDenseTCIndex(fedId, econTId, 0, tcidx) 
			     << ", getDenseTCIndex01: " << moduleIndexer.getDenseTCIndex(fedId, econTId+1, 1, tcidx) 
			     << ", getDenseTCIndex02: " << moduleIndexer.getDenseTCIndex(fedId, econTId+2, 2, tcidx) 
			     << ", getIndexForModuleData00: " << moduleIndexer.getIndexForModuleData(fedId, econTId, tcidx) 
			     << ", getIndexForModuleData01: " << moduleIndexer.getIndexForModuleData(fedId, econTId+1, tcidx) 
			     << ", getIndexForModuleData02: " << moduleIndexer.getIndexForModuleData(fedId, econTId+2, tcidx) 
			     << std::endl;
		  LogDebug("[HGCalUnpackerTrigger]")  << "HGCalUnpackerTrigger::parseFEDData "
			     << " algo = " << uint16_t(digisTrigger.view()[denseIdx].algo())
			     << ", valid = " << uint16_t(digisTrigger.view()[denseIdx].valid()(bx,0))
			     << ", nBxs = " << uint16_t(digisTrigger.view()[denseIdx].nBxs())
			     << ", nTCs = " << uint16_t(digisTrigger.view()[denseIdx].nTCs())
			     << ", ieconTId = " << uint32_t(digisTrigger.view()[denseIdx].econTId())
			     << std::endl;
		  LogDebug("[HGCalUnpackerTrigger]")  << "HGCalUnpackerTrigger::parseFEDData ibx : " << bx
			     << ", bxID : " << uint16_t(digisTrigger.view()[denseIdx].bxId()(bx,0))
			     << ", MS/totE : " << uint32_t(digisTrigger.view()[denseIdx].TotE()(bx,0))
			     << std::endl;
		  LogDebug("[HGCalUnpackerTrigger]")  << "HGCalUnpackerTrigger::parseFEDData itc : " << itc
			     << ", Address: " << uint16_t(digisTrigger.view()[denseIdx].TCAddress()(bx,0))
			     << ", Unpacked Energy: " << uint32_t(digisTrigger.view()[denseIdx].TCEnergy()(bx,0))
			     << std::endl;
		  denseIdx++;
		}	      
		//denseIndexOffset += rdp.size();
		nprevTxs += neTx;
	      }//iecon loop

	      // /////////////////////////// Si ////////////////////////////
	    }else{
	      /// The following part of Sci will be trated same way as the Si after the CONFIGURE ATION is advanced 
	      /////////////////////////// Sci ////////////////////////////
	      const int neTx1 = 4;
	      const int neTx2 = 3;
	      uint32_t *el1 = new uint32_t[neTx1];
	      uint32_t *el2 = new uint32_t[neTx2];
	      el1[0] = elinks[1];
	      el1[1] = elinks[0];
	      el1[2] = elinks[2];
	      el1[3] = elinks[3];
	      
	      el2[0] = elinks[4];
	      el2[1] = elinks[5];
	      el2[2] = elinks[6];
	      
	      //Run 111137 and 111138
	      // el[0] = elinks[6];
	      // el[1] = elinks[5];
	      // el[2] = elinks[4];
	      // if(neTx>3) el[3] = elinks[3];
	      
	      for(unsigned iel(0);iel<neTx1;iel++){
		std::cout << "\t\t el1 " << std::setw(3) << iel << " = 0x"
			  << std::hex << std::setfill('0')
			  << std::setw(8) << el1[iel]
			  << std::dec << std::setfill(' ')
			  << std::endl;	      		
	      }
	      
	      for(unsigned iel(0);iel<neTx2;iel++){
		std::cout << "\t\t el1 " << std::setw(3) << iel << " = 0x"
			  << std::hex << std::setfill('0')
			  << std::setw(8) << el2[iel]
			  << std::dec << std::setfill(' ')
			  << std::endl;	      		
	      }
	      
	      TPGFEDataformat::TcRawDataPacket rdp1, rdp2;
	      TPGStage1Emulation::Stage1IO::convertElinksToTcRawData(TPGFEDataformat::STC4A, 12, el1, rdp1);
	      rdp1.print();
	      TPGStage1Emulation::Stage1IO::convertElinksToTcRawData(TPGFEDataformat::STC4A, 10, el2, rdp2);
	      rdp2.print();

	      uint64_t tot0 = 0, tot1 = 0;
	      for(const auto& itc: rdp1.getTcData()) tot0 += itc.decodedE(rdp1.type());
	      for(const auto& itc: rdp2.getTcData()) tot1 += itc.decodedE(rdp2.type());
	      std::cout << "tot0: " << tot0 << ", tot1: " << tot1 << std::endl;
	      
	      delete []el1;
	      delete []el2;
	      /////////////////////////// Sci ////////////////////////////
	    }
	    delete []elinks;
	  }
	  econTOffset += nEconTs;
	}//list of valid emp channel
      }
	
      tsh=tsh->nextSubpacketHeader();
    }
    noffecafe++;
  }
  
  return true;
}

bool HGCalUnpackerTrigger::parseTDAQBlock(){
  return true;
}
