#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/SourceFactory.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/ESProducer.h"
#include "FWCore/Framework/interface/EventSetupRecordIntervalFinder.h"
#include "FWCore/Framework/interface/ESProducts.h"
#include "FWCore/Utilities/interface/ESGetToken.h"
#include "FWCore/Utilities/interface/do_nothing_deleter.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/FileInPath.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"

#include "CondFormats/HGCalObjects/interface/HGCALTriggerConfiguration.h"
#include "CondFormats/HGCalObjects/interface/HGCalMappingModuleIndexerTrigger.h"
#include "CondFormats/DataRecord/interface/HGCalElectronicsMappingRcd.h"
#include "CondFormats/DataRecord/interface/HGCalModuleConfigurationRcd.h"  // depends on HGCalElectronicsMappingRcd
#include "RecoLocalCalo/HGCalRecAlgos/interface/HGCalESProducerTools.h"    // for json, search_modkey, search_fedkey

#include <string>   // for std::to_string
#include <fstream>  // needed to read json file with std::ifstream

/**
 * @short ESProducer to parse HGCAL electronics configuration from JSON file
 */
class HGCALTriggerConfigurationESProducer : public edm::ESProducer, public edm::EventSetupRecordIntervalFinder {
public:
  explicit HGCALTriggerConfigurationESProducer(const edm::ParameterSet& iConfig)
      :  //edm::ESProducer(iConfig),
        fedjson_(iConfig.getParameter<std::string>("fedjson")),
        modjson_(iConfig.getParameter<std::string>("modjson")) {
    auto cc = setWhatProduced(this);
    indexToken_ = cc.consumes(iConfig.getParameter<edm::ESInputTag>("indexSource"));
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::ESInputTag>("indexSource", edm::ESInputTag(""))
        ->setComment("Label for module indexer to set SoA size");
    desc.add<std::string>("fedjson", "")->setComment("JSON file with FED configuration parameters");
    desc.add<std::string>("modjson", "")->setComment("JSON file with ECOND configuration parameters");
    descriptions.addWithDefaultLabel(desc);
  }

  // @short get hexadecimal value, and override if value_override>=0
  static int32_t gethex(const std::string& value, const int32_t value_override) {
    return (value_override >= 0 ? value_override : std::stoi(value, nullptr, 16));
  }

  // @short get integer value, and override if value_override>=0
  static int32_t getint(const int32_t value, const int32_t value_override) {
    return (value_override >= 0 ? value_override : value);
  }

  std::unique_ptr<HGCALTriggerConfiguration> produce(const HGCalModuleConfigurationRcd& iRecord) {
    auto const& moduleMap = iRecord.get(indexToken_);
    edm::LogInfo("HGCALTriggerConfigurationESProducer")
        << "produce: fedjson_=" << fedjson_ << ",\n         modjson_=" << modjson_;

    // retrieve values from custom JSON format (see HGCalCalibrationESProducer)
    edm::FileInPath fedfip(fedjson_);  // e.g. HGCalCommissioning/LocalCalibration/data/config_feds.json
    edm::FileInPath modfip(modjson_);  // e.g. HGCalCommissioning/LocalCalibration/data/config_econds.json
    std::ifstream fedfile(fedjson_);
    std::ifstream modfile(modjson_);
    const json fed_config_data = json::parse(fedfile, nullptr, true, /*ignore_comments*/ true);
    const json mod_config_data = json::parse(modfile, nullptr, true, /*ignore_comments*/ true);

    // consistency check
    uint32_t nfeds = moduleMap.numFEDs();
    // const std::vector<std::string> fedkeys = {"mismatchPassthroughMode", "cbHeaderMarker", "slinkHeaderMarker"};
    const std::vector<std::string> fedkeys = {"tdaqHeaderMarker", "tdaqFlag", "neconts", "econtSwapOffset"};
    // const std::vector<std::string> modkeys = {"headerMarker", "CalibrationSC"};
    const std::vector<std::string> modkeys = {"density", "dropLSB", "select", "stc_type", "eporttx_numen","calv","mux"};
    if (nfeds != fed_config_data.size())
      edm::LogWarning("HGCALTriggerConfigurationESProducer")
          << "Total number of FEDs found in JSON file " << fedjson_ << " (" << fed_config_data.size()
          << ") does not match indexer (" << nfeds << ")";

    // loop over FEDs in indexer & fill configuration structs: FED > ECON-D > eRx
    // follow indexing by HGCalMappingModuleIndexer
    // HGCALTriggerConfiguration = container class holding FED structs of ECON-D structs of eRx structs
    std::unique_ptr<HGCALTriggerConfiguration> config_ = std::make_unique<HGCALTriggerConfiguration>();
    config_->feds.resize(moduleMap.maxFEDSize());
    for (std::size_t fedid = 0; fedid < moduleMap.maxFEDSize(); fedid++) {
      // sanity checks
      const auto fedkey = hgcal::search_fedkey(fedid, fed_config_data, fedjson_);  // search matching key
      hgcal::check_keys(
          fed_config_data, fedkey, fedkeys, fedjson_);  // check required keys are in the JSON, warn otherwise

      if (moduleMap.fedReadoutSequences()[fedid].readoutTypes_.empty())            // check if FED exists (non-empty)
        continue;                                                                  // skip non-existent FED
      if (fed_config_data[fedkey]['tdaqFlag'].size() != fed_config_data[fedkey]['neconts'].size()) // check if tdaqFlag and neconts have the same length(number of TDAQs)
        continue;
      uint32_t nTDAQ = fed_config_data[fedkey]['tdaqFlag'].size();
      uint32_t totalECONTs=0;
      for (std::size_t itdaq=0;i<nTDAQ;itdaq++){
        totalECONTs+=fed_config_data[fedkey]['neconts'][itdaq]
      }
      if (moduleMap.getNumModules(fedid) != fed_config_data[fedkey]['econtSwapOffset'].size()
        || moduleMap.getNumModules(fedid) != totalECONTs)             // check if length of sawp offsets, number of ECONTs in FED read from module locator, and number of econts summed mathces
        continue;

      // fill FED configurations
      HGCalTriggerFedConfig fedConfig;
      
      // fill econtSwapOffset
      fedConfig.econtSwapOffest.resize(moduleMap.getNumModules(fedid));
      for (std::size_t iecont=0;iecont<moduleMap.getNumModules(fedid); iecont++){
        fedConfig.econtSwapOffset[iecont] = fed_config_data[fedkey]['econtSwapOffset'][iecont]
      }
      // fill TDAQ configurations
      fedConfig.tdaqs.resize(nTDAQ);
      totalECONTs = 0;
      for (std::size_t itdaq=0;i<nTDAQ;itdaq++){
        HGCalTDAQConfig tdaqConfig;
        tdaqConfig.tdaqBlockHeaderMarker=std::stoi(fed_config_data[fedkey]['tdaqHeaderMarker'], nullptr, 16);
        tdaqConfig.tdaqFlag=fed_config_data[fedkey]['tdaqFlag'][itdaq];
        uint32_t nECONT = fed_config_data[fedkey]['neconts'];

        for (const auto& [typecode, ids] : moduleMap.typecodeMap()) {
          auto [fedid_, imod] = ids;
          if (fedid_ != fedid && totalECONTs<=imod && imod<totalECONTs+nECONT)
            continue;
          const auto modkey = hgcal::search_modkey(typecode, mod_config_data, modjson_);  // search matching key
          hgcal::check_keys(
              mod_config_data, modkey, modkeys, modjson_);  // check required keys are in the JSON, warn otherwise
          //sanity check
          nTC_calv = mod_config_data[modkey]["calv"].size();
          nTC_mux = mod_config_data[modkey]["tcMux"].size();
          nTC = moduleMap.getNumChannels(typecode)
          if(nTC != nTC_mux || nTC != nTC_calv){
            continue;
          }
          HGCalECONTConfig econtConfig;
          econtConfig.density = mod_config_data[modkey]["density"];
          econtConfig.dropLSB = mod_config_data[modkey]["dropLSB"];
          econtConfig.select = mod_config_data[modkey]["select"];
          econtConfig.stcType = mod_config_data[modkey]["stcType"];
          econtConfig.eportTxNumen = mod_config_data[modkey]["eportTxNumen"];

          econtConfig.calv.resize(nTC);
          econtConfig.tcMux.resize(nTC);
          econtConfig.offset.resize();
          for(std::size_t iTC;iTC<nTC;iTC++){
            econtConfig.calv[iTC] = mod_config_data[modkey]['calv'][iTC];
            econtConfig.tcMux[iTC] = mod_config_data[modkey]['tcMux'][iTC];
            econtConfig.offset[iTC] = calculateCellOffset(); //TODO: change this when we know how to calcualte
          }
          // Caculate module number in the TDAQ
          iecont = imod - totalECONTs;
          tdaqConfig.econts[iecont] = econtConfig;
        }
        fedConfig.tdaqs[itdaq]=tdaqConfig;
        totalECONTs += fed_config_data[fedkey]['neconts'][itdaq];
      }
      config_->feds[fedid] = fed;
    }
    std::cout<<config_<<std::endl;
    return config_;
  }  // end of produce()

private:
  uint32_t calculateCellOffset(){
    return 0;
  }
  void setIntervalFor(const edm::eventsetup::EventSetupRecordKey&,
                      const edm::IOVSyncValue&,
                      edm::ValidityInterval& oValidity) override {
    oValidity = edm::ValidityInterval(edm::IOVSyncValue::beginOfTime(), edm::IOVSyncValue::endOfTime());
  }

  edm::ESGetToken<HGCalMappingModuleIndexer, HGCalElectronicsMappingRcd> indexToken_;
  const std::string fedjson_;       // JSON file
  const std::string modjson_;       // JSON file
};

DEFINE_FWK_EVENTSETUP_SOURCE(HGCALTriggerConfigurationESProducer);
