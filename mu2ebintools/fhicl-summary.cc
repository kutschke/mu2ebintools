// Print summary information about the overall structure of a fcl file.
// Original author Rob Kutschke

#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

#include "fhiclcpp/intermediate_table.h"
#include "fhiclcpp/parse.h"
#include "fhiclcpp/ParameterSet.h"
#include "fhiclcpp/make_ParameterSet.h"

#include "cetlib/filepath_maker.h"

std::string usage() {
  return "Usage: fhicl-summary [-v|-vv] file.fcl\n";
}

typedef std::vector<std::string> strlist;

// Descriptive strings used in printout.
constexpr char const* const notPresent = "<not present>";
constexpr char const* const empty      = "<present but empty>";

// Information about modules that are in the configuration
struct ModuleInfo {
  std::string label;
  std::string module_type;
  ModuleInfo( std::string const& l, std::string const& t):label(l),module_type(t){
    maxlen_label = std::max( maxlen_label, label.size() );
  }

  // For use in formated printout: to align the "label : type" pairs on the colon.
  static size_t maxlen_label;
};

size_t ModuleInfo::maxlen_label=0;

inline std::ostream& operator<<(std::ostream& os,
                                const ModuleInfo& id ){
  os << std::setw(ModuleInfo::maxlen_label) << id.label << " : " << id.module_type;
  return os;
}

// Information about each path.
class PathInfo{

public:

  // Types of paths.
  enum type { ambiguous, trigger, end };

  PathInfo( std::string const& n,
            strlist const&     m,
            type               t ):name(n), module_labels(m), pathType(t){
  }

  std::string name;
  strlist     module_labels;
  type        pathType = ambiguous;

  static std::string const& typeName( PathInfo::type t ){
    static std::array<std::string,3> names { "ambiguous", "trigger", "end" };
    return names.at(t);
  }

};

inline std::ostream& operator<<(std::ostream& os,
                                const PathInfo& info ){
  os << info.name;
  return os;
}


// The workhorse class: organizes information from the parameter set.
class FclSummary{
public:

  FclSummary(  const fhicl::ParameterSet& pset );

  void verbosity0( std::string const&, std::ostream& os ) const;
  void verbosity1( std::string const&, std::ostream& os ) const;
  void verbosity2( std::string const&, std::ostream& os ) const;

  void classifyPaths();

private:
  const fhicl::ParameterSet& _pset;

  // Those names that are special to art when found inside
  // the physics parameter set.
  strlist _reservedToArtPhysics;

  // Information found in the parameter set.
  std::string _process_name;
  std::string _source_module_type;

  // Information about selected fhicl parameters.
  bool _hasServices = false;
  strlist _services;

  bool _hasOutputs = false;
  strlist _outputs;
  std::vector<ModuleInfo>  _outputModuleInfo;

  bool _hasPhysics = false;
  strlist _physics;

  bool _hasAnalyzers = false;
  strlist _analyzers;
  std::vector<ModuleInfo>  _analyzerModuleInfo;

  bool _hasProducers = false;
  strlist _producers;
  std::vector<ModuleInfo>  _producerModuleInfo;

  bool _hasFilters = false;
  strlist _filters;
  std::vector<ModuleInfo>  _filterModuleInfo;

  bool _hasTrigger_paths = false;
  strlist _trigger_paths;

  bool _hasEnd_paths = false;
  strlist _end_paths;

  // All identifiers in the physics parameter set that are
  // not on the reserved list and are, therefore, candidates
  // being a path.
  strlist _path_candidates;

  // Subsets of _path_candidates, sorted by trigger/end ambiguous.
  std::array<std::vector<PathInfo>,3> _pathInfo;

};

// The following free functions are helpers used by FclSummary.

// Given a parameter set and the name of a table inside that parameter set, return
// the names of the identifiers in that table as a return argument.
bool getNames( fhicl::ParameterSet const& pset, std::string tableName, strlist& names ){
  fhicl::ParameterSet tmp;
  bool hasIt = pset.get_if_present<fhicl::ParameterSet>( tableName, tmp );
  if ( hasIt ){
    names = tmp.get_names();
  }
  return hasIt;
}

// Given a parameter set that contains module configurations and some
// identifiers of module labels, fill an output data structure with
// the module_type for each module label.  The identifiers are presented
// in two parts as a basename plus a module label, for example:
//  basename: physics.producers  label: g4run
//
void getModuleInfo( fhicl::ParameterSet const& pset,
                    std::string const&         basename,
                    strlist const&             moduleLabels,
                    std::vector<ModuleInfo>&   info ){

  for ( auto const& label : moduleLabels ){
    std::string key = basename + "." + label + ".module_type";
    std::string type;
    bool hasType = pset.get_if_present<std::string>(key,type);
    if ( hasType ){
      info.emplace_back( label, type);
    } else {
      info.emplace_back( label, std::string(notPresent));
    }
  }
}

// Given a collection of names and a collection of reserved names,
// return a collection of all names from the first collection that
// are not present in the list of reserved names.
void removeReservedNames( strlist const& allNames,
                          strlist const& reservedNames,
                          strlist&       remainingNames ){

  for ( auto const& s : allNames ){
    auto i = find( reservedNames.begin(), reservedNames.end(), s);
    if ( i == reservedNames.end() ){
      remainingNames.emplace_back(s);
    }
  }
}

// Helper function for FclSummary::classifyPaths.
bool foundIn( std::string const& name, std::vector<ModuleInfo> const& infos){
  for ( auto const& i : infos ){
    if ( i.label == name ){
      return true;
    }
  }
  return false;
}

// 4-input logical XOR: true if and only if exactly one of the inputs is true;
// C++ does not have a logical XOR builtin; only a 2 argument bitwise XOR.
bool logicalXOR ( bool b0, bool b1, bool b2, bool b3){
  int n{0};
  if ( b0 ) ++n;
  if ( b1 ) ++n;
  if ( b2 ) ++n;
  if ( b3 ) ++n;
  return (n==1);
}

// Classify a module based on what type of path it may be on: trigger path or end path.
PathInfo::type classifyModule ( bool prod, bool filt, bool anal, bool outp){
  bool isTrigger{false};
  bool isEnd{false};
  if ( logicalXOR( prod, filt, anal, outp ) ){
    isTrigger = (prod || filt) ;
    isEnd     = (anal || outp);
    if ( isTrigger ) return PathInfo::trigger;
    if ( isEnd     ) return PathInfo::end;
  }
  return PathInfo::ambiguous;
}

// Helper for verbosity0 printing.
void printSize ( std::ostream& os, bool has, std::string name, strlist const& s ){
  if ( has ){
    os << name << s.size() << std::endl;
  } else {
    os << name << std::string(notPresent) << std::endl;
  }
}

// Helper for verbosity1 and verbosity2 printing.
template <class T>
void printInfo ( std::ostream& os, bool has, std::string name, T const& s ){
  if ( has ){
    if ( s.empty() ){
      os << name << std::string(empty) << std::endl;
    } else{
      os << name << s.at(0) << std::endl;
      std::string pad( name.size(), ' ');
      for ( size_t i=1; i<s.size(); ++i ){
        os << pad << s.at(i) << std::endl;
      }
    }
  } else {
    os << name << std::string(notPresent) << std::endl;
  }
}

// All work to collect information is done in the c'tor
FclSummary::FclSummary(  const fhicl::ParameterSet& pset ):
  _pset(pset),
  _reservedToArtPhysics{"analyzers","producers","filters","trigger_paths", "end_paths"},
  _process_name(notPresent),
  _source_module_type(notPresent){

  _pset.get_if_present( "process_name", _process_name);
  _pset.get_if_present( "source.module_type", _source_module_type);

  _hasServices = getNames( _pset, "services", _services );
  _hasOutputs  = getNames( _pset, "outputs",  _outputs  );
  _hasPhysics  = getNames( _pset, "physics",  _physics  );

  if ( _hasPhysics ){
    _hasProducers  = getNames( _pset, "physics.producers",  _producers  );
    _hasAnalyzers  = getNames( _pset, "physics.analyzers",  _analyzers  );
    _hasFilters    = getNames( _pset, "physics.filters",    _filters  );
  }
  _hasTrigger_paths = _pset.get_if_present<strlist>("physics.trigger_paths", _trigger_paths);
  _hasEnd_paths     = _pset.get_if_present<strlist>("physics.end_paths",     _end_paths);

  getModuleInfo( _pset, "outputs",           _outputs,   _outputModuleInfo   );
  getModuleInfo( _pset, "physics.producers", _producers, _producerModuleInfo );
  getModuleInfo( _pset, "physics.analyzers", _analyzers, _analyzerModuleInfo );
  getModuleInfo( _pset, "physics.filters",   _filters,   _filterModuleInfo   );

  // Select candidate path names from the names in the physics parameter set.
  removeReservedNames( _physics, _reservedToArtPhysics, _path_candidates);

  // Classify the candidate path names as either trigger paths, end paths or ambiguous.
  classifyPaths();

}

// Printout with no verbosity option specfied.
void FclSummary::verbosity0( std::string const& filename, std::ostream& os ) const{

  os << "filename:      " << filename << std::endl;
  os << "process name:  " << _process_name << std::endl;
  os << "source module: " << _source_module_type << std::endl;
  printSize ( os, _hasOutputs,       "outputs:       ", _outputs   );
  printSize ( os, _hasProducers,     "services:      ", _services  );
  printSize ( os, _hasProducers,     "producers:     ", _producers );
  printSize ( os, _hasAnalyzers,     "analyzers:     ", _analyzers );
  printSize ( os, _hasFilters,       "filters:       ", _filters );
  printSize ( os, _hasTrigger_paths, "trigger_paths: ", _trigger_paths );
  printSize ( os, _hasEnd_paths,     "end_paths:     ", _end_paths );
  printSize ( os, !_path_candidates.empty(),
              "paths:         ", _path_candidates );

}

// Printout for -v
void FclSummary::verbosity1( std::string const& filename, std::ostream& os ) const{

  os << "filename:        " << filename << std::endl;
  os << "process name:    " << _process_name << std::endl;
  os << "source module:   " << _source_module_type << std::endl;
  printInfo( os, _hasOutputs, "outputs:         ", _outputModuleInfo );
  printInfo( os, _hasServices, "services:        ", _services );

  printSize ( os, _hasProducers,     "producers:       ", _producers );
  printSize ( os, _hasAnalyzers,     "analyzers:       ", _analyzers );
  printSize ( os, _hasFilters,       "filters:         ", _filters );
  printSize ( os, _hasTrigger_paths, "trigger_paths:   ", _trigger_paths );
  printSize ( os, _hasEnd_paths,     "end_paths:       ", _end_paths );

  printInfo ( os, !_pathInfo.at(PathInfo::trigger).empty(),   "trigger paths:   ", _pathInfo.at(PathInfo::trigger)   );
  printInfo ( os, !_pathInfo.at(PathInfo::end).empty(),       "end paths:       ", _pathInfo.at(PathInfo::end)       );
  printInfo ( os, !_pathInfo.at(PathInfo::ambiguous).empty(), "ambiguous paths: ", _pathInfo.at(PathInfo::ambiguous) );
}

// Printout for -vv
void FclSummary::verbosity2( std::string const& filename, std::ostream& os ) const{

  os << "filename:        " << filename << std::endl;
  os << "process name:    " << _process_name << std::endl;
  os << "source module:   " << _source_module_type << std::endl;

  printInfo( os, _hasOutputs,  "outputs:         ", _outputModuleInfo );
  printInfo( os, _hasServices, "services:        ", _services );

  printInfo ( os, _hasProducers,     "producers:       ", _producerModuleInfo );
  printInfo ( os, _hasAnalyzers,     "analyzers:       ", _analyzerModuleInfo );
  printInfo ( os, _hasFilters,       "filters:         ", _filterModuleInfo );
  printInfo ( os, _hasTrigger_paths, "trigger_paths:   ", _trigger_paths );
  printInfo ( os, _hasEnd_paths,     "end_paths:       ", _end_paths );

  printInfo ( os, !_pathInfo.at(PathInfo::trigger).empty(),   "trigger paths:   ", _pathInfo.at(PathInfo::trigger)   );
  printInfo ( os, !_pathInfo.at(PathInfo::end).empty(),       "end paths:       ", _pathInfo.at(PathInfo::end)       );
  printInfo ( os, !_pathInfo.at(PathInfo::ambiguous).empty(), "ambiguous paths: ", _pathInfo.at(PathInfo::ambiguous) );

}

int main(int argc, const char* argv[]){

  if( (argc > 1) &&
      ((argv[1] == std::string("-h")) ||
       (argv[1] == std::string("--help"))) ) {
    std::cout<<usage();
    exit(0);
  }

  try {

    // Check and parse arguments.
    if ( argc != 2 && argc != 3){
      throw std::runtime_error("Error: wrong number of parameters.\n" + usage());
    }
    const std::string opt = ( argc == 2 ) ? std::string() : (argv[1]);
    const std::string infile = (argc == 2 ) ? std::string(argv[1]) : std::string(argv[2]);

    // Read input file to make a parameter set.
    cet::filepath_lookup_after1 policy("FHICL_FILE_PATH");

    fhicl::intermediate_table tbl;
    fhicl::parse_document(infile, policy, tbl);

    fhicl::ParameterSet pset;
    fhicl::make_ParameterSet(tbl, pset);

    // Extract information from the parameter set
    FclSummary summary( pset);

    // Decide what to print
    if ( opt.empty() ) {
      summary.verbosity0(infile, std::cout);
    } else if ( opt == "-v" ){
      summary.verbosity1(infile, std::cout);
    } else if ( opt == "-vv" ){
      summary.verbosity2(infile, std::cout);
    } else {
      throw std::runtime_error("Error: unrecognized parameter: " + opt + "\n" + usage());
    }

  }
  catch(std::exception& e) {
    std::cerr<<e.what()<<std::endl;
    exit(1);
  }

  exit(0);
}

// Organize _path_candidates into 3 groups: trigger paths, end paths
// and ambiguous paths.
void FclSummary::classifyPaths() {

  for ( auto const& pathName : _path_candidates ){
    auto const& moduleLabels = _pset.get<strlist>("physics."+pathName);

    // Classify each module label as elligible to be on a trigger path
    // or on an end path and count the number of each.
    std::array<int,3> moduleTypes{0, 0, 0};
    for ( auto const& moduleLabel : moduleLabels ){
      bool prod = foundIn( moduleLabel, _producerModuleInfo );
      bool filt = foundIn( moduleLabel, _filterModuleInfo );
      bool anal = foundIn( moduleLabel, _analyzerModuleInfo );
      bool outp = foundIn( moduleLabel, _outputModuleInfo );
      auto type  = classifyModule( prod, filt, anal, outp);
      ++moduleTypes.at(type);
    }

    // Classify the path based on PathInfo::type of their modules.
    PathInfo::type type{PathInfo::ambiguous};
    if ( moduleTypes.at(PathInfo::ambiguous) == 0 &&
         moduleTypes.at(PathInfo::trigger)    > 0 &&
         moduleTypes.at(PathInfo::end)       == 0 ){
      type = PathInfo::trigger;
    } else if ( moduleTypes.at(PathInfo::ambiguous) == 0 &&
                moduleTypes.at(PathInfo::trigger)   == 0 &&
                moduleTypes.at(PathInfo::end)        > 0 ){
      type = PathInfo::end;
    }

    // Save information for use by printing code.
    _pathInfo.at(type).emplace_back( pathName, moduleLabels, type);

  }
}
