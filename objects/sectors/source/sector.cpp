/*! 
* \file sector.cpp
* \ingroup Objects
* \brief Sector class source file.
* \author Sonny Kim, Steve Smith, Josh Lurz
* \date $Date$
* \version $Revision$
*/

#include "util/base/include/definitions.h"
#include <string>
#include <iostream>
#include <fstream>
#include <cassert>

// xml headers
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <xercesc/dom/DOMNode.hpp>
#include <xercesc/dom/DOMNodeList.hpp>

#include "util/base/include/xml_helper.h"
#include "sectors/include/sector.h"
#include "sectors/include/subsector.h"
#include "containers/include/scenario.h"
#include "util/base/include/model_time.h"
#include "marketplace/include/marketplace.h"
#include "util/base/include/configuration.h"
#include "util/base/include/summary.h"
#include "emissions/include/indirect_emiss_coef.h"
#include "containers/include/world.h"
#include "util/base/include/util.h"
#include "containers/include/region.h"
#include "util/logger/include/ilogger.h"
#include "marketplace/include/market_info.h"

using namespace std;
using namespace xercesc;

extern Scenario* scenario;

/*! \brief Default constructor.
*
* Constructor initializes member variables with default values, sets vector sizes, and sets value of debug flag.
*
* \author Sonny Kim, Steve Smith, Josh Lurz
*/
Sector::Sector( const string regionNameIn ): regionName( regionNameIn ){
    initElementalMembers();
    Configuration* conf = Configuration::getInstance();
    debugChecking = conf->getBool( "debugChecking" );

    // resize vectors
    const Modeltime* modeltime = scenario->getModeltime();
    const int maxper = modeltime->getmaxper();

    sectorprice.resize( maxper );
    input.resize( maxper ); // Sector total energy consumption
    output.resize( maxper ); // total amount of final output from Sector
    fixedOutput.resize( maxper );
    summary.resize( maxper ); // object containing summaries
    capLimitsPresent.resize( maxper, false ); // flag for presence of capacity limits
}

/*! \brief Destructor
* \details Deletes all subsector objects associated  with this Sector.
* \author Josh Lurz
*/
Sector::~Sector() {
    clear();
}

//! Clear member variables
void Sector::clear(){
    for( vector<Subsector*>::iterator subSecIter = subsec.begin(); subSecIter != subsec.end(); subSecIter++ ) {
        delete *subSecIter;
    }
}

//! Initialize elemental data members.
void Sector::initElementalMembers(){
    nosubsec = 0;
    tax = 0;
    anyFixedCapacity = false;
	CO2EmFactor = 0;
}

/*! \brief Returns Sector name
*
* \author Sonny Kim
* \return Sector name as a string
*/
string Sector::getName() const {
    return name;
}

/*! \brief Set data members from XML input
*
* \author Josh Lurz
* \param node pointer to the current node in the XML input tree
* \todo josh to add appropriate detailed comment here
*/
void Sector::XMLParse( const DOMNode* node ){
    /*! \pre make sure we were passed a valid node. */
    assert( node );

    // get the name attribute.
    name = XMLHelper<string>::getAttrString( node, "name" );

    // get additional attributes for derived classes
    XMLDerivedClassParseAttr( node );

    // get all child nodes.
    DOMNodeList* nodeList = node->getChildNodes();
    const Modeltime* modeltime = scenario->getModeltime();

    // loop through the child nodes.
    for( unsigned int i = 0; i < nodeList->getLength(); i++ ){
        DOMNode* curr = nodeList->item( i );
        string nodeName = XMLHelper<string>::safeTranscode( curr->getNodeName() );

        if( nodeName == "#text" ) {
            continue;
        }
        else if( nodeName == "market" ){
            market = XMLHelper<string>::getValueString( curr ); // only one market element.
        }
        else if( nodeName == "price" ){
            XMLHelper<double>::insertValueIntoVector( curr, sectorprice, modeltime );
        }
        else if( nodeName == "output" ) {
            XMLHelper<double>::insertValueIntoVector( curr, output, modeltime );
        }
        else if( nodeName == "unit" ) {
            unit = XMLHelper<string>::getValueString( curr );
        }
		else if( nodeName == Subsector::getXMLNameStatic() ){
            parseContainerNode( curr, subsec, subSectorNameMap, new Subsector( regionName, name ) );
        }
        else if( XMLDerivedClassParse( nodeName, curr ) ){
        }
        else {
            ILogger& mainLog = ILogger::getLogger( "main_log" );
            mainLog.setLevel( ILogger::WARNING );
            mainLog << "Unrecognized text string: " << nodeName << " found while parsing " << getXMLName() << "." << endl;
        }
    }
}

/*! \brief Complete the initialization
*
* This routine is only called once per model run
*
* \author Josh Lurz
* \warning markets are not necesarilly set when completeInit is called
*/
void Sector::completeInit() {
    // Allocate the sector info.
    mSectorInfo.reset( new MarketInfo() );

    nosubsec = static_cast<int>( subsec.size() );
    
    // Check if the market string is blank, if so default to the region name.
    if( market == "" ){
        ILogger& mainLog = ILogger::getLogger( "main_log" );
        mainLog.setLevel( ILogger::NOTICE );
        mainLog << "No marketname set in " << regionName << "->" << name << ". Defaulting to regional market." << endl;
        market = regionName;
    }
    
    // Complete the subsector initializations. 
    for( vector<Subsector*>::iterator subSecIter = subsec.begin(); subSecIter != subsec.end(); subSecIter++ ) {
        ( *subSecIter )->completeInit();
    }
    
    // Set markets for this sector
    setMarket();
}

/*! \brief Write object to xml output stream
*
* Method writes the contents of this object to the XML output stream.
*
* \author Josh Lurz
* \param out reference to the output stream
* \param tabs A tabs object responsible for printing the correct number of tabs. 
*/
void Sector::toInputXML( ostream& out, Tabs* tabs ) const {
    const Modeltime* modeltime = scenario->getModeltime();

	XMLWriteOpeningTag ( getXMLName(), out, tabs, name );

    // write the xml for the class members.
    // write out the market string.
    XMLWriteElement( market, "market", out, tabs );
    XMLWriteElement( unit, "unit", out, tabs );

    for( int i = 0; modeltime->getper_to_yr( i ) <= 1975; i++ ){
        XMLWriteElementCheckDefault( sectorprice[ i ], "price", out, tabs, 0.0, modeltime->getper_to_yr( i ) );
    }

    for( int i = 0; modeltime->getper_to_yr( i ) <= 1975; i++ ){
        XMLWriteElement( output[ i ], "output", out, tabs, modeltime->getper_to_yr( i ) );
    }

    // write out variables for derived classes
    toInputXMLDerived( out, tabs );

	// write out the subsector objects.
    for( vector<Subsector*>::const_iterator k = subsec.begin(); k != subsec.end(); k++ ){
        ( *k )->toInputXML( out, tabs );
    }

	// finished writing xml for the class members.
	XMLWriteClosingTag( getXMLName(), out, tabs );
}


/*! \brief Write output (selected output?) from this object to XML
*
* I don't know how this is different from the previous method.....
*
* \author Josh Lurz
* \param out reference to the output stream
* \param tabs A tabs object responsible for printing the correct number of tabs.
* \todo josh to update documentation on this method ..
*/
void Sector::toOutputXML( ostream& out, Tabs* tabs ) const {
    const Modeltime* modeltime = scenario->getModeltime();

	XMLWriteOpeningTag ( getXMLName(), out, tabs, name );

    // write the xml for the class members.
    // write out the market string.
    XMLWriteElement( market, "market", out, tabs );
    XMLWriteElement( unit, "unit", out, tabs );

    for( int i = 0; i < static_cast<int>( sectorprice.size() ); i++ ){
        XMLWriteElement( sectorprice[ i ], "price", out, tabs, modeltime->getper_to_yr( i ) );
    }

    for( int i = 0; i < static_cast<int>( output.size() ); i++ ){
        XMLWriteElement( output[ i ], "output", out, tabs, modeltime->getper_to_yr( i ) );
    }

    // write out variables for derived classes
    toOutputXMLDerived( out, tabs );
	
	// write out the subsector objects.
    for( vector<Subsector*>::const_iterator k = subsec.begin(); k != subsec.end(); k++ ){
        ( *k )->toInputXML( out, tabs );
    }

    // finished writing xml for the class members.
	XMLWriteClosingTag( getXMLName(), out, tabs );
}

/*! \brief Write information useful for debugging to XML output stream
*
* Function writes market and other useful info to XML. Useful for debugging.
*
* \author Josh Lurz
* \param period model period
* \param out reference to the output stream
* \param tabs A tabs object responsible for printing the correct number of tabs.
*/
void Sector::toDebugXML( const int period, ostream& out, Tabs* tabs ) const {

	XMLWriteOpeningTag ( getXMLName(), out, tabs, name );

    // write the xml for the class members.
    // write out the market string.
    XMLWriteElement( market, "market", out, tabs );
    XMLWriteElement( unit, "unit", out, tabs );

    // Write out the data in the vectors for the current period.
    XMLWriteElement( sectorprice[ period ], "sectorprice", out, tabs );
    XMLWriteElement( input[ period ], "input", out, tabs );
    XMLWriteElement( output[ period ], "output", out, tabs );

	toDebugXMLDerived (period, out, tabs);

	// Write out the summary
    // summary[ period ].toDebugXML( period, out );

    // write out the subsector objects.
    for( vector<Subsector*>::const_iterator j = subsec.begin(); j != subsec.end(); j++ ){
        ( *j )->toDebugXML( period, out, tabs );
    }

    // finished writing xml for the class members.

	XMLWriteClosingTag( getXMLName(), out, tabs );
}

/*! \brief Perform any initializations needed for each period.
*
* Any initializations or calcuations that only need to be done once per period (instead of every iteration) should be placed in this function.
*
* \author Steve Smith
* \param period Model period
*/
void Sector::initCalc( const int period, const MarketInfo* aRegionInfo ) {

    // normalizeShareWeights must be called before subsector initializations
    normalizeShareWeights( period );
    
    // do any sub-Sector initializations
    for ( int i=0; i<nosubsec; i++ ) {
        subsec[ i ]->initCalc( period, mSectorInfo.get() );
    }

    // set flag if there are any fixed supplies
    if ( getFixedOutput( period ) > 0 ) {
        anyFixedCapacity = true;
    }

    // find out if this Sector has any capacity limits
    capLimitsPresent[ period ] = isCapacityLimitsInSector( period );

    // check to see if previous period's calibrations were set ok
    // If this portion of the code spits out a warning if all supplies and demand
	 //  are calibrated but not equal
	 // A common cause for this warnbing is if a calibration value was left out of an add-on file
	 // 
	 // Accurate calibration of base-year values requires a few extra world->Calc()'s after solving 
	 // to make sure share weights have been adjusted to be consistant with final solution prices
	 //
	 // If debugchecking flag is on extra information is printed
   // if ( Configuration::getInstance()->getBool( "debugChecking" ) && period > 0 ) { 
   //    isAllCalibrated( period - 1 , true );
   //  }
   // THIS CALL now takes place in the solver
}

/*! \brief Perform any sector level calibration data consistancy checks
*
* \author Steve Smith
* \param period Model period
*/
void Sector::checkSectorCalData( const int period ) {

    for( vector<Subsector*>::const_iterator j = subsec.begin(); j != subsec.end(); j++ ){
        ( *j )->checkSubSectorCalData( period );
    }
}

/*! \brief check for fixed demands and set values to counter
*
* Routine flows down to technoogy and sets fixed demands to the appropriate marketplace to be counted
*
* \author Steve Smith
* \param period Model period
*/
void Sector::tabulateFixedDemands( const int period ) {

    for( vector<Subsector*>::const_iterator j = subsec.begin(); j != subsec.end(); j++ ){
        ( *j )->tabulateFixedDemands( period );
    }
}

/*! \brief Scales sub-sector share weights so that they equal number of subsectors.
*
* This is needed so that 1) share weights can be easily interpreted (> 1 means favored) and so that
* future share weights can be consistently applied relative to calibrated years.
*
* \author Steve Smith
* \param period Model period
* \warning This must be done before subsector inits so that share weights are scaled before they are interpolated
*/
void Sector::normalizeShareWeights( const int period ) {

    // If this sector was completely calibrated, or otherwise fixed, then scale shareweights to equal number of subsectors
    if  ( period > 0 && Configuration::getInstance()->getBool( "CalibrationActive" ) ) {
        if ( inputsAllFixed( period - 1, name ) && ( getCalOutput ( period - 1) > 0 ) ) {

            double shareWeightTotal = 0;
            int numberNonzeroSubSectors = 0;
            for ( int i=0; i<nosubsec; i++ ) {
                double subsectShareWeight = subsec[ i ]->getShareWeight( period - 1 );
                shareWeightTotal += subsectShareWeight;
                if ( subsectShareWeight > 0 ) {
                    numberNonzeroSubSectors += 1;
                }
            }

            if ( shareWeightTotal < util::getTinyNumber() ) {
                ILogger& mainLog = ILogger::getLogger( "main_log" );
                mainLog.setLevel( ILogger::ERROR );
                mainLog << "ERROR: in sector " << name << " Shareweights sum to zero." << endl;
            } else {
                for ( int i=0; i<nosubsec; i++ ) {
                    subsec[ i ]->scaleShareWeight( numberNonzeroSubSectors / shareWeightTotal, period - 1 );
                }
                ILogger& mainLog = ILogger::getLogger( "main_log" );
                mainLog.setLevel( ILogger::DEBUG );
                    // sjsTEMP. Turn this on once data is updated. Also move notice to separate log.
             //   mainLog << "Shareweights normalized for sector " << name << " in region " << regionName << endl;
            }
        }
    }
}

/*! \brief Test to see if calibration worked for this sector
*
* Compares the sum of calibrated + fixed values to output of sector.
* Will optionally print warning to the screen (and eventually log file).
* 
* If all outputs are not calibrated then this does not check for consistancy.
*
* \author Steve Smith
* \param period Model period
* \calAccuracy Accuracy to check if calibrations are within.
* \param printWarnings if true prints a warning
* \return Boolean true if calibration is ok.
*/
bool Sector::isAllCalibrated( const int period, double calAccuracy, const bool printWarnings ) const {	
   const bool PRINT_DETAILS = false; // toggle for more detailed debugging output
        
   bool checkCalResult = true; 
   if ( period > 0 && Configuration::getInstance()->getBool( "CalibrationActive" ) ) {
      double calOutputs = getCalOutput( period );
      double totalFixed = calOutputs + getFixedOutput( period );
      double calDiff =  totalFixed - getOutput( period );

      // Two cases to check for. If outputs are all fixed, then calDiff should be small in either case.
      // Even if outputs are not all fixed, then calDiff shouldn't be > calAccuracy (i.e., totalFixedOutputs > actual output)
      if ( calOutputs > 0 ) {
         double diffFraction = calDiff/calOutputs;
         if ( ( calDiff > calAccuracy ) || ( ( abs(diffFraction) > calAccuracy ) && outputsAllFixed( period ) ) ) {
            checkCalResult = false;
            if ( printWarnings ) {
               ILogger& mainLog = ILogger::getLogger( "main_log" );
               mainLog.setLevel( ILogger::WARNING );
               mainLog << "WARNING: " << name << " " << " in " << regionName << " != cal+fixed vals (";
               mainLog << totalFixed << " )" << " in yr " <<  scenario->getModeltime()->getper_to_yr( period );
               mainLog << " by: " << calDiff << " (" << calDiff*100/calOutputs << "%) " << endl;
               if ( PRINT_DETAILS) {
                  cout << "   fixedSupplies: " << "  "; 
                  for ( int i=0; i<nosubsec; i++ ) {
                     double fixedSubSectorOut =  subsec[ i ]->getTotalCalOutputs( period ) + subsec[ i ]->getFixedOutput( period );
                     cout << "ss["<<i<<"] "<< fixedSubSectorOut << ", ";
                  } 
                  cout << endl;
                  cout << "   Production: " << "  "; 
                  for ( int i=0; i<nosubsec; i++ ) {
                     cout << "ss["<<i<<"] "<< subsec[ i ]->getOutput( period ) << ", ";
                  } 
                  cout << endl;
               }
            }
         } // calDiff > calAccuracy branch
      }
   }
   return checkCalResult;
}

/*!
* \brief  Calculate subsector shares, adjusting for capacity limits.

* This routine calls subsector::calcShare for each subsector, which calculated an unnormalized 
* share, and then calls normShare to normalize the shares for each subsector.
* The code below also takes into account sectors with fixed output. The sectors without fixed 
* output are normalized to sum/(1-fixedSum) and the sectors with fixed output are reset to their
* fixed share. Note that the fixed share is an approximation, held over from the last iteration, 
* of the actual share of any technology with a fixed output. 
*
* \param period model period
* \param gdp GDP object used to calculate various types of GDPs.
* \author Sonny Kim, Steve Smith, Josh Lurz
* \todo add warnings to sub-Sector and technology (?) that fixed capacity has to be >0
* \warning model with fixed capacity in sectors where demand is not a solved market may not solve
*/
void Sector::calcShare( const int period, const GDP* gdp ) {
    // Note that this solution for the fixed capacity share problem requires that 
    // simultaneity be turned on. This would seem to be because the fixed share is lagged one period
    // and can cause an oscillation. With the demand for this Sector in the marketplace, however, the
    // fixed capacity converges as the trial value for demand converges. Region::findSimul now checks for this.
    
    double sum = 0;
    double fixedSum = 0;
	 
    // first loop through all subsectors to get the appropriate sums
    for ( unsigned int i = 0; i < subsec.size(); i++ ) {
		// calculate subsector shares (based on technology shares)
		subsec[ i ]->calcShare( period, gdp );

		// sum fixed capacity separately, but don't bother with the extra code if this Sector has none
		// Calculation re-ordered to eliminate subtraction of fixed share from sum which eliminated 
		// a share <> 1 warning when initial (non-fixed) sum was extremely small (2/04 - sjs)
		double fixedShare = 0;
		if ( anyFixedCapacity ) {
			fixedShare = getFixedShare( i , period );
			fixedSum += fixedShare; // keep track of total fixed shares
		}

		// Sum shares that are not fixed
		if ( fixedShare < util::getTinyNumber() ) {
			sum += subsec[ i ]->getShare( period );
		}
		
		// initialize cap limit status as false for this sector (will be changed in adjSharesCapLimit if necessary)
		subsec[ i ]->setCapLimitStatus( false , period );
    }

    // Take care of case where fixed share is > 1
    double scaleFixedShare = 1;
    if ( fixedSum > 1 ) {
        scaleFixedShare = 1/fixedSum;
        fixedSum = 1;
    }

    // Now normalize shares
    for ( unsigned int i = 0; i < subsec.size(); i++ ) {

		if ( subsec[ i ]->getFixedOutput( period ) == 0 ) {
			// normalize subsector shares that are not fixed
			if ( fixedSum < 1 ) {
	 			subsec[ i ]->normShare( sum / ( 1 - fixedSum ) , period );	
			} else {
				subsec[ i ]->normShare( sum / util::getTinyNumber() , period );	// if all fixed supply, eliminate other shares
                // Could this overflow if sum was large?-JPL
			}

		// reset share of sectors with fixed supply to their appropriate value
		} else {
            double fixedShare = getFixedShare( i , period ) * scaleFixedShare;
            double currentShare = subsec[ i ]->getFixedShare( period );
            
            subsec[ i ]->setShareToFixedValue( period );
            if ( currentShare > 0 ) { 
                subsec[ i ]->scalefixedOutput( fixedShare/currentShare, period ); 
            }
            subsec[ i ]->setShareToFixedValue( period );
		}
    }

    // Now adjust for capacity limits
    // on 10/22/03 adding this check saves about 1/40 of the model run time.
    if ( capLimitsPresent[ period ] ) {
        adjSharesCapLimit( period );
    }

    // Check to make sure shares still equal 1
    if ( debugChecking ) {
        checkShareSum( period );
    }
}

/*!
* \brief Determine if any capacity limits are exceeded and adjust for shares if so.
*
* If a capacity limit comes into play the routine shifts the "excess" share (over the capacity limits) to the non-limited sectors. This routine loops several times in case this shift then causes another Sector to exceed a capacity limit.
*
* Sectors that have fixed outputs are not adjusted.
* 
* The logic for the share adjustment is as follows:
*     Sum_notlim(Si) + Sum_limited(Si) + sumSharesOverLimit = 1
*     where:
*         Sum_notlim(Si)  = sum of shares that are not capacity limited
*         Sum_limited(Si) = sum of shares that are capacity limited
*         sumSharesOverLimit = portion of shares that were over capacity limits
*         
*     Need to solve for the amount to increase shares to account for capacity limits
*    if:
*         newSi = a * Si, then a few lines of algebra gives:
*         a = 1 + sumSharesOverLimit/Sum_notlim(Si)
*
* \author Steve Smith
* \warning The routine assumes that shares are already normalized.
* \param period Model period
*/
void Sector::adjSharesCapLimit( const int period ) {

    const double SMALL_NUM = util::getSmallNumber();
    double tempCapacityLimit;
    double tempSubSectShare;
    double totalFixedShares = 0;
    bool capLimited = true; // true if any sub-sectors are over their capacity limit
    //bool capLimited = false; // set to false to turn cap limits off for testing
    int i=0;

    // check for capacity limits, repeating to take care of any knock-on effects. 
    // Do this a maximum of times equal to number of subsectors, 
    // which is the maximum number of times could possibly need to do this
    for (int n = 0;  n < nosubsec && capLimited; n++) {
        double sumSharesOverLimit = 0.0;		// portion of shares over cap limits
        double sumSharesNotLimited = 0.0;	// sum of shares not subject to cap limits
        capLimited = false;

        //  Check for capacity limits and calculate sums, looping through each subsector
        for ( i=0; i<nosubsec; i++ ) {
            double actualCapacityLimit = subsec[ i ]->getCapacityLimit( period ); // call once, store these locally
            tempSubSectShare = subsec[ i ]->getShare( period ) ;

            // if Sector has been cap limited, then return limit, otherwise transform
            // this is needed because can only do the transform once
            if ( subsec[ i ]->getCapLimitStatus( period ) ) {
                tempCapacityLimit = subsec[ i ]->getShare( period );
            } 
			else {
                tempCapacityLimit = Subsector::capLimitTransform( actualCapacityLimit, tempSubSectShare );
            }

            // if there is a capacity limit and are over then set flag and count excess shares
            if ( tempSubSectShare - tempCapacityLimit > SMALL_NUM ) {
                capLimited = true;
                sumSharesOverLimit += tempSubSectShare - tempCapacityLimit;

                //           cout << "Cap limit changed from " << actualCapacityLimit << " to " << tempCapacityLimit;
                //  cout << " in sub-Sector: " << subsec[ i ]->getName() << endl;

            }

            // also sum shares under limit (but not those just at their limits)
            if ( tempSubSectShare < tempCapacityLimit ) {
                sumSharesNotLimited += tempSubSectShare;
            }

            // But don't count shares that have fixed outputs. 
            // Sectors with fixed outputs are not adjusted in subsec::limitShares below.
            if ( subsec[ i ]->getFixedShare( period ) > 0 ) {
                sumSharesNotLimited -= tempSubSectShare;
                totalFixedShares += subsec[ i ]->getFixedShare( period );
            }

        } // end of loop over sub-sectors

        // re-normalize subsector shares if capacity limits have been exceeded
        // See comments above for derivation of multiplier
        if ( capLimited ) {
            if ( sumSharesNotLimited > 0 ) {
                for ( i=0; i<nosubsec; i++ ) {
                    double multiplier = 1 + sumSharesOverLimit/sumSharesNotLimited;
                    subsec[ i ]->limitShares( multiplier, period );
                }
            }
            else { // If there are no sectors without limits and there are still shares to be re-distributed
                if ( sumSharesOverLimit > 0 ) {
                    // if there is no shares left then too much was limited!
                    cerr << regionName << ": Insufficient capacity to meet demand in Sector " << name << endl;
                }
            }
        }
    } // end for loop

    // if have exited and still capacity limited, then report error
    if ( capLimited ) {
        cerr << "Capacity limit not resolved in Sector " << name << endl;
    }
}

/*! \brief Check that sum of shares is equal to one.
*
* Routine used for checking. Prints an error if shares do not sum to one. 
* Good to run if debugChecking flag is on.
*
* \author Steve Smith
* \param period Model period
*/
void Sector::checkShareSum( const int period ) const {
    const double SMALL_NUM = util::getSmallNumber();
    double sumshares = 0;
    int i;

    for ( i=0; i<nosubsec; i++ ) {
        // Check the validity of shares.
        assert( util::isValidNumber( subsec[ i ]->getShare( period ) ) );
        sumshares += subsec[ i ]->getShare( period ) ;
    }
    if ( fabs(sumshares - 1) > SMALL_NUM ) {
        cerr << "ERROR: Shares do not sum to 1. Sum = " << sumshares << " in Sector " << name;
        cerr << ", region: " << regionName << endl;
        cout << "Shares: ";
        for ( i=0; i<nosubsec; i++ ) {
            cout << subsec[ i ]->getShare( period ) << ", ";
        }
        cout << endl;
    }
}

/*! \brief Calculate weighted average price of subsectors.
*
* weighted price is put into sectorprice variable
*
* \author Sonny Kim, Josh Lurz, James Blackwood
* \param period Model period
*/
void Sector::calcPrice( const int period ) {
    Marketplace* marketplace = scenario->getMarketplace();
    sectorprice[ period ]= 0;
	CO2EmFactor = 0; // reinitialize to 0 everytime
    for (int i=0;i<nosubsec;i++) {	
        sectorprice[ period ] += subsec[ i ]->getShare( period ) * subsec[ i ]->getPrice( period );
        CO2EmFactor += subsec[ i ]->getShare( period ) * subsec[ i ]->getCO2EmFactor( period );
    }
	if (marketplace->doesMarketExist( name, regionName, period )) {
	    marketplace->setMarketInfo(name,regionName,period,"CO2EmFactor",CO2EmFactor);
	}
}

/*! \brief Calculate the final supply price.
* \details Calculates shares for the sector, then sets the price of the good
* into the marketplace.
*/
void Sector::calcFinalSupplyPrice( const GDP* gdp, const int period ){
    calcShare( period, gdp );
    calcPrice( period );
    // set market price of intermediate goods
    Marketplace* marketplace = scenario->getMarketplace();
    marketplace->setPrice( name, regionName, sectorprice[ period ], period );
}

/*! \brief returns the Sector price.
*
* Returns the weighted price. 
* Calcuation of price incorporated into call to ensure that this is calculated.
* 
* \author Sonny Kim
* \param period Model period
*/
double Sector::getPrice( const int period ) {
    calcPrice( period );
    return sectorprice[ period ];
}

/*! \brief Returns true if all sub-Sector outputs are fixed or calibrated.
*
* Routine loops through all the subsectors in the current Sector. If output is calibrated, 
* assigned a fixed output, or set to zero (because share weight is zero) then true is returned. 
* If all ouptput is not fixed, then the Sector has at least some capacity to respond to a change in prices.
*
* \author Steve Smith
* \param period Model period
* \return Boolean that is true if entire Sector is calibrated or has fixed output
*/
bool Sector::outputsAllFixed( const int period ) const {

	if ( period < 0 ) {
		return false;
	} 
	else {
		for ( int i=0; i<nosubsec; i++ ) {
			if ( !(subsec[ i ]->allOuputFixed( period )) ) {
					return false;
			}
		}
	}

    return true;
}

/*! \brief Returns true if any sub-sectors have capacity limits.
*
* Routine checks to see if any capacity limits are present in the sub-sectors. Is used to avoid calling capacity limit calculation unnecessary. *
* \author Steve Smith
* \param period Model period
* \return Boolean that is true if there are any capacity limits present in any sub-Sector
*/
bool Sector::isCapacityLimitsInSector( const int period ) const {
    bool anyCapacityLimits = false;

    if ( period < 0 ) {
        anyCapacityLimits = false;
    } 
	else {
        for ( int i = 0; i < nosubsec && !anyCapacityLimits; i++ ) {
            if ( !( subsec[ i ]->getCapacityLimit( period ) == 1 ) ) {
                anyCapacityLimits = true;
            }
        }
    }
    return anyCapacityLimits;
}

/*! \brief Adds final supply for the sector to the marketplace.
*/
void Sector::setFinalSupply( const int period ){
    // supply and demand for intermediate and final good are set equal
    double marketSupply = updateAndGetOutput( period );

    // set market supply of intermediate goods
    Marketplace* marketplace = scenario->getMarketplace();
    marketplace->addToSupply( name, regionName, marketSupply, period );
}

//! Set output for Sector (ONLY USED FOR energy service demand at present).
/*! Demand from the "dmd" parameter (could be energy or energy service) is passed to subsectors.
This is then shared out at the technology level.
In the case of demand, what is passed here is the energy service demand. 
The technologies convert this to an energy demand.
The demand is then summed at the subsector level (subsec::sumOutput) then
later at the Sector level (in region via supplysector[j].sumOutput( per ))
to equal the total Sector output.
*
* \author Sonny Kim, Josh Lurz, Steve Smith
* \param demand Demand to be passed to the subsectors. (Sonny check this)
* \param period Model period
* \param gdp GDP object uses to calculate various types of GDPs.
*/
void Sector::setoutput( const double demand, const int period, const GDP* gdp ) {
    for ( int i=0; i<nosubsec; i++ ) {
        // set subsector output from Sector demand
        subsec[ i ]->setoutput( demand, period, gdp );
    }
}

/*! \brief Return subsector fixed Supply.
*
* Returns the total amount of fixed supply from all subsectors and technologies.
*
* \author Steve Smith
* \param period Model period
* \param printValues Toggle to print out each value for debugging (default false)
* \return total fixed supply
*/
double Sector::getFixedOutput( const int period, bool printValues ) const {
    double totalfixedOutput = 0;
    for ( int i=0; i<nosubsec; i++ ) {
        totalfixedOutput += subsec[ i ]->getFixedOutput( period );
		  if ( printValues ) { cout << "sSubSec["<<i<<"] "<< subsec[ i ]->getFixedOutput( period ) << ", "; } 
    }
    return totalfixedOutput;
}

/*! \brief Returns the share of fixed supply from the given subsector using a particular logic, depending on model setup.
*
* This function returns either the saved sub-Sector share, or the share as derived from the marketplace demand, if available.
*
* \author Steve Smith
* \todo This function should be in subsector, not sector-JPL
* \param subsectorNum Subsector number
* \param period Model period
* \return total fixed supply
* \warning Not sure how well using market demand will work if multiple sectors are adding demands. 
*/
double Sector::getFixedShare( const int subsectorNum, const int period ) const {
    Marketplace* marketplace = scenario->getMarketplace();

    if ( subsectorNum >= 0 && subsectorNum < nosubsec ) {
        double fixedShare = subsec[ subsectorNum ]->getFixedShare( period );
        if ( fixedShare > 0) {
            // if demand is available through marketplace then use this instead of lagged value
            double mktDmd = marketplace->getDemand( name, regionName, period );
            if ( mktDmd > 0 ) {
                fixedShare = subsec[ subsectorNum ]->getFixedOutput( period ) / mktDmd;
            }
        }
        return fixedShare;
    } 
	else {
        cerr << "Illegal Subsector number: " << subsectorNum << endl;
        return 0;
    }
}

/*! \brief Return subsector total calibrated outputs.
*
* Returns the total calibrated outputs from all subsectors and technologies. 
* Note that any calibrated input values are converted to outputs and are included.
*
* This returns only calibrated outputs, not values otherwise fixed (as fixed or zero share weights)
*
* \author Steve Smith
* \param period Model period
* \return total calibrated outputs
*/
double Sector::getCalOutput( const int period  ) const {
    double totalCalOutput = 0;
    for ( int i=0; i<nosubsec; i++ ) {
        totalCalOutput += subsec[ i ]->getTotalCalOutputs( period );
    }
    return totalCalOutput;
}

/*! \brief Return subsector total fixed or calibrated inputs.
*
* Returns the total fixed inputs from all subsectors and technologies. 
* Note that any calibrated output values are converted to inputs and are included.
*
* \author Steve Smith
* \param period Model period
* \param goodName market good to return inputs for. If equal to the value "allInputs" then returns all inputs.
* \param bothVals optional parameter. It true (default) both calibration and fixed values are returned, if false only calInputs
* \return total fixed inputs
*/
double Sector::getCalAndFixedInputs( const int period, const std::string& goodName, const bool bothVals ) const {
    double totalFixedInput = 0;
    for ( int i=0; i<nosubsec; i++ ) {
        totalFixedInput += subsec[ i ]->getCalAndFixedInputs( period, goodName, bothVals );
    }
    return totalFixedInput;
}

/*! \brief Return subsector total fixed or calibrated inputs.
*
* Returns the total fixed inputs from all subsectors and technologies. 
* Note that any calibrated output values are converted to inputs and are included.
*
* \author Steve Smith
* \param period Model period
* \param goodName market good to return inputs for. If equal to the value "allInputs" then returns all inputs.
* \param bothVals optional parameter. It true (default) both calibration and fixed values are returned, if false only calInputs
* \return total fixed inputs
*/
double Sector::getCalAndFixedOutputs( const int period, const std::string& goodName, const bool bothVals ) const {
    double sumCalOutputValues = 0;
    for ( int i=0; i<nosubsec; i++ ) {
        sumCalOutputValues += subsec[ i ]->getCalAndFixedOutputs( period, goodName, bothVals );
    }
    return sumCalOutputValues;
}

/*! \brief Calculates the input value needed to produce the required output
*
* \author Steve Smith
* \param period Model period
* \param goodName market good to determine the inputs for.
* \param requiredOutput Amount of output to produce
*/
void Sector::setImpliedFixedInput( const int period, const std::string& goodName, const double requiredOutput ) {
    bool inputWasChanged = false;
    for ( int i=0; i<nosubsec; i++ ) {
        if ( !inputWasChanged ) {
            inputWasChanged = subsec[ i ]->setImpliedFixedInput( period, goodName, requiredOutput );
        } else {
            bool tempChange = subsec[ i ]->setImpliedFixedInput( period, goodName, requiredOutput );
            if ( tempChange ) {
                ILogger& mainLog = ILogger::getLogger( "main_log" );
                mainLog.setLevel( ILogger::NOTICE );
                mainLog << "  WARNING: caldemands for more than one subsector were changed " ; 
                mainLog << " in sector " << name << " in region " << regionName << endl; 
            }
        }
    }
}

/*! \brief Returns true if all subsector inputs for the the specified good are fixed.
*
* Fixed inputs can be by either fixedCapacity, calibration, or zero share. 
*
* \author Steve Smith
* \param period Model period
* \param goodName market good to return inputs for. If equal to the value "allInputs" then returns all inputs.
* \return total calibrated inputs
*/
bool Sector::inputsAllFixed( const int period, const std::string& goodName ) const {
	
		for ( int i=0; i<nosubsec; i++ ) {
        if ( !(subsec[ i ]->inputsAllFixed( period, goodName ) ) ){
			return false;
		}
	}
	return true;
}

/*! \brief Scales calibrated values for the specified good.
*
* \author Steve Smith
* \param period Model period
* \param goodName market good to return inputs for
* \param scaleValue multipliciative scaler for calibrated values 
* \return total calibrated inputs
*/
void Sector::scaleCalibratedValues( const int period, const std::string& goodName, const double scaleValue ) {
	
	for ( int i=0; i<nosubsec; i++ ) {
		subsec[ i ]->scaleCalibratedValues( period, goodName, scaleValue );
	}
}

/*! \brief Calibrate Sector output.
*
* This performs supply Sector technology and sub-Sector output/input calibration. 
Determines total amount of calibrated and fixed output and passes that down to the subsectors.

* Note that this routine only performs subsector and technology-level calibration. Total final energy calibration is done by Region::calibrateTFE and GDP calibration is set up in Region::calibrateRegion.
*
* \author Steve Smith
* \param period Model period
*/
void Sector::calibrateSector( const int period ) {
    Marketplace* marketplace = scenario->getMarketplace();
    double totalfixedOutput;
    double totalCalOutputs;
    double mrkdmd;

    totalfixedOutput = getFixedOutput( period ); 
    mrkdmd = marketplace->getDemand( name, regionName, period ); // demand for the good produced by this Sector
    totalCalOutputs = getCalOutput( period );

    for (int i=0; i<nosubsec; i++ ) {
        if ( subsec[ i ]->getCalibrationStatus( period ) ) {
            subsec[ i ]->adjustForCalibration( mrkdmd, totalfixedOutput, totalCalOutputs, outputsAllFixed( period ), period );
        }
    }
} 

/*! \brief Adjust shares to be consistant with fixed supply
*
* This routine determines the total amount of fixed supply in this Sector and adjusts other shares to be consistant with the fixed supply.  If fixed supply exceeds demand then the fixed supply is reduced. An internal variable with the Sector share of fixed supply for each sub-Sector is set so that this information is available to other routines.

* \author Steve Smith
* \param marketDemand demand for the good produced by this Sector
* \param period Model period
* \warning fixed supply must be > 0 (to obtain 0 supply, set share weight to zero)
*/
void Sector::adjustForFixedOutput( const double marketDemand, const int period ) {
    int i;
    double totalfixedOutput = 0; 
    double variableShares = 0; // original sum of shares of non-fixed subsectors   
    double variableSharesNew = 0; // new sum of shares of non-fixed subsectors   
    double shareRatio;  // ratio for adjusting shares of non-fixed subsectors

    // set output from technologies that have fixed outputs such as hydro electricity

    // Determine total fixed production and total var shares
    // Need to change the exog_supply function once new, general fixed supply method is available
    totalfixedOutput = 0;
    for ( i=0; i<nosubsec; i++ ) {
        double fixedOutput = 0;
        subsec[ i ]->resetfixedOutput( period );
        fixedOutput = subsec[ i ]->getFixedOutput( period );

        // initialize property to zero every time just in case fixed share property changes 
        // (shouldn't at the moment, but that could allways change)
        subsec[ i ]->setFixedShare( period, 0 ); 

        // add up subsector shares without fixed output
        // sjs -- Tried treating capacity limited sub-sectors differently, here and in adjShares,
        //     -- but that didn't give capacity limits exactly.
        if ( fixedOutput == 0 ) { 
            variableShares += subsec[ i ]->getShare( period );
        } 
		else {
            if ( marketDemand != 0 ) {
                double shareVal = fixedOutput / marketDemand;
                if ( shareVal > 1 ) { 
                    shareVal = 1; // Eliminates warning message since this conditionshould be fixed below
                } 
                subsec[ i ]->setFixedShare( period, shareVal ); // set fixed share property
            }
        }
        totalfixedOutput += fixedOutput;
    }

    // Scale down fixed output if its greater than actual demand
    if ( totalfixedOutput > marketDemand ) {
        for ( i = 0; i < nosubsec; i++ ) {
            subsec[ i ]->scalefixedOutput( marketDemand / totalfixedOutput, period ); 
        }
        totalfixedOutput = marketDemand;
    }

    /*// debugging check
    // sjs TEMP -- this check generally spits out a few innocuous warnings.
    // As of ver 1.0, this happens only in Latin America when fixed supply has temporarily
    // exceeded demand and has been scaled back.  (found by putting cout statement in Sector::getFixedShare)
    // Code has been left in just in case something seems to be going wrong
    // If simultunaeities are resolved then this should only happen a couple times per iteration.
    if ( debugChecking && world->getCalibrationSetting() && 1==2) {
        if ( fabs(fixedShareSavedVal - totalfixedOutput/marketDemand) > 1e-5 && fixedShareSavedVal != 0 ) {
            cerr << "Fixed share changed from " << fixedShareSavedVal << " to ";
            cerr << totalfixedOutput/marketDemand << endl;
            cout << "  -- in region: " << regionName << " sector: " << name << endl;
            if (regionName == "Latin America" ) {
               cout << "    totalfixedOutput: " << totalfixedOutput;
               cout << "    marketDemand: " << marketDemand << endl;
            }
      }
    }
    */
    // Adjust shares for any fixed output
    if (totalfixedOutput > 0) {
        if (totalfixedOutput > marketDemand ) {            
            variableSharesNew = 0; // should be no variable shares in this case
        }
        else {
            assert( marketDemand != 0); // check for 0 so that variableSharesNew does not blow up
            variableSharesNew = 1 - (totalfixedOutput/ marketDemand );
        }

        if (variableShares == 0) {
            shareRatio = 0; // in case all subsectors are fixed output, unlikely
        }
        else {
            shareRatio = variableSharesNew/variableShares;
        }

        // now that parameters are set, adjust shares for all sub-sectors
        for ( i=0; i<nosubsec; i++ ) {
            // shareRatio = 0 is okay, sets all non-fixed shares to 0
            subsec[ i ]->adjShares( marketDemand, shareRatio, totalfixedOutput, period ); 
        }
    }
}

/*! \brief Set supply Sector output
*
* This routine takes the market demand and propagates that through the supply sub-sectors
where it is shared out (and subsequently passed to the technology level within each sub-Sector
to be shared out).

Routine also calls adjustForFixedOutput which adjusts shares, if necessary, for any fixed output sub-sectors.

* \author Sonny Kim
* \param period Model period
* \param gdp GDP object uses to calculate various types of GDPs.
*/
void Sector::supply( const int period, const GDP* gdp ) {
    Marketplace* marketplace = scenario->getMarketplace();
    double mrkdmd = marketplace->getDemand( name, regionName, period ); // demand for the good produced by this Sector

    if ( mrkdmd < 0 ) {
        cerr << "ERROR: Demand value < 0 for good " << name << " in region " << regionName << endl;
    }

    // Adjust shares for fixed supply
    if ( anyFixedCapacity ) {
        adjustForFixedOutput( mrkdmd, period );
    }

    // This is where subsector and technology outputs are set
    for (int i=0;i<nosubsec;i++) {
        // set subsector output from Sector demand
        subsec[ i ]->setoutput( mrkdmd, period, gdp );
    }    

    if ( debugChecking ) {
        // If the model is working correctly this should never give an error
        // An error here means that the supply summed up from the supply sectors 
        // is not equal to the demand that was passed in 
        double mrksupply = updateAndGetOutput( period ); // debugChecking modifying values is dangerous.

        // if demand identically = 1 then must be in initial iteration so is not an error
        if ( period > 0 && fabs(mrksupply - mrkdmd) > 0.01 && mrkdmd != 1 ) {
            mrksupply = mrksupply * 1.000000001;
            cout << regionName << " Market "<<  name<< " demand and derived supply are not equal by: ";
            cout << fabs(mrksupply - mrkdmd) << ": ";
            cout << "S: " << mrksupply << "  D: " << mrkdmd << endl;

            if ( 0 ) { // detailed debugging output
                for (int i=0;i<nosubsec;i++) {
                    cout << subsec[ i ]->getName() << " Share:";
                    cout << subsec[ i ]->getShare( period ) << " Output:";
                    cout << subsec[ i ]->getOutput( period ) << endl;
                }
            }
        }
    }
}

/*! \brief Sum subsector outputs.
*
* \author Sonny Kim, Josh Lurz
* \param period Model period
*/
void Sector::sumOutput( const int period ) {
    output[ period ] = 0;
    for ( int i=0; i<nosubsec; i++ ) {
		// getOutput() calls subsector summing routine
        double temp = subsec[ i ]->getOutput( period );
        output[ period ] += temp;

        // error check. Move this before += and do not add.
        if ( !util::isValidNumber( temp ) ){
            cerr << "output for subSector "<< i << "(" << subsec[ i ]->getName() << ")" <<" is not valid, with value " << temp;
            cerr << " in Sector: " << name << " Region: " << regionName << endl;
        }
    }
}

/*! \brief returns Sector output.
*
* Returns the total amount of the Sector. 
* 
* Routine now incorporates sumOutput so that output is automatically correct.
*
* \author Sonny Kim
* \param period Model period
* \todo make year 1975 regular model year so that logic below can be removed
* \return total output
*/
double Sector::updateAndGetOutput( const int period ) {

    // this is needed because output for 1975 is hard coded at the Sector level for some sectors,
    // in which case do not want to sum.
    if ( period > 0 || output[ period ] == 0 ) {
        sumOutput( period );
    }
    return output[ period ]; 
}

/*! \brief returns Sector output.
*
* Returns the total amount of the Sector. 
* 
* Routine now incorporates sumOutput so that output is automatically correct.
*
* \author Sonny Kim
* \param period Model period
* \todo make year 1975 regular model year so that logic below can be removed
* \return total output
*/
double Sector::getOutput( const int period ) const {
    return output[ period ]; 
}

/*! \brief Calculate GHG emissions for each Sector from subsectors.
*
* Calculates emissions for subsectors and technologies, then updates emissions maps for emissions by gas and emissions by fuel & gas.
*
* Note that at present (10/03), emissions only occur at technology level.
*
* \author Sonny Kim
* \param period Model period
*/
void Sector::emission( const int period ) {
    summary[ period ].clearemiss(); // clear emissions map
    summary[ period ].clearemfuelmap(); // clear emissions fuel map
    for ( int i = 0;i<nosubsec;i++) {
        subsec[ i ]->emission( period );
        summary[ period ].updateemiss( subsec[ i ]->getemission( period )); // by gas
        summary[ period ].updateemfuelmap( subsec[ i ]->getemfuelmap( period )); // by fuel and gas
    }
}

/*! \brief Calculate indirect GHG emissions for each Sector from subsectors.
*
* \author Sonny Kim
* \param period Model period
* \param emcoef_ind Vector of indirect emissions objects. 
*/
void Sector::indemission( const int period, const vector<Emcoef_ind>& emcoef_ind ) {
    summary[ period ].clearemindmap(); // clear emissions map
    for ( int i = 0; i<nosubsec;i++) {
        subsec[ i ]->indemission( period, emcoef_ind );
        summary[ period ].updateemindmap( subsec[ i ]->getemindmap( period ));
    }
}

/*! \brief Sums subsector primary and final energy consumption.
*
* Routine sums all input energy consumption and puts that into the input variable.
*
* \author Sonny Kim, Josh Lurz
* \param period Model period
*/
void Sector::sumInput( const int period ) {
    input[ period ] = 0;
    for (int i=0;i<nosubsec;i++) {
        input[ period ] += subsec[ i ]->getInput( period );
    }
}

/*! \brief Returns sectoral energy consumption.
*
* Routine sums all input energy consumption and puts that into the input variable.
* Sector input is now summed every time this function is called.
*
* \author Sonny Kim
* \param period Model period
* \return total input
*/
double Sector::getInput( const int period ) {
    sumInput( period );
    return input[ period ];
}

/*! \brief Returns sectoral energy consumption.
*
* Returns all input for energy sectors.
*
* \author Steve Smith
* \param period Model period
* \return total input
*/
double Sector::getEnergyInput( const int period ) {
    return getInput( period );
}

//! Write Sector output to database.
void Sector::csvOutputFile() const {
    // function protocol
    void fileoutput3( string var1name,string var2name,string var3name,
        string var4name,string var5name,string uname,vector<double> dout);

    // function arguments are variable name, double array, db name, table name
    // the function writes all years
    // total Sector output
    fileoutput3( regionName, name, " ", " ", "production", "EJ",output);
    // total Sector eneryg input
    fileoutput3( regionName, name, " ", " ", "consumption", "EJ",input);
    // Sector price
    fileoutput3( regionName, name, " ", " ", "price", "$/GJ", sectorprice);
    // Sector carbon taxes paid
    const Modeltime* modeltime = scenario->getModeltime();
    const int maxper = modeltime->getmaxper();
    vector<double> temp(maxper);
    for( int per = 0; per < maxper; ++per ){
        temp[ per ] = getTotalCarbonTaxPaid( per );
    }
    fileoutput3( regionName, name, " ", " ", "C tax paid", "Mil90$", temp );
}

//! Write MiniCAM style Sector output to database.
void Sector::dbOutput() const {
    const Modeltime* modeltime = scenario->getModeltime();
    // function protocol
    void dboutput4(string var1name,string var2name,string var3name,string var4name,
        string uname,vector<double> dout);
    int m;

    // total Sector output
    dboutput4( regionName,"Secondary Energy Prod","by Sector",name,"EJ",output);
    dboutput4( regionName,"Secondary Energy Prod",name,"zTotal","EJ",output);

    int maxper = modeltime->getmaxper();
    vector<double> temp(maxper);
    string str; // temporary string

    // Sector fuel consumption by fuel type
    typedef map<string,double>:: const_iterator CI;
    map<string,double> tfuelmap = summary[0].getfuelcons();
    for (CI fmap=tfuelmap.begin(); fmap!=tfuelmap.end(); ++fmap) {
        for (int m=0;m<maxper;m++) {
            temp[m] = summary[m].get_fmap_second(fmap->first);
        }
        if( fmap->first == "" ){
            dboutput4( regionName,"Fuel Consumption",name, "No Fuelname", "EJ",temp);
        }
        else {
            dboutput4( regionName,"Fuel Consumption",name,fmap->first,"EJ",temp);
        }
    }

    // Sector emissions for all greenhouse gases
    map<string,double> temissmap = summary[0].getemission(); // get gases for per 0
    for (CI gmap=temissmap.begin(); gmap!=temissmap.end(); ++gmap) {
        for (int m=0;m<maxper;m++) {
            temp[m] = summary[m].get_emissmap_second(gmap->first);
        }
        dboutput4(regionName,"Emissions","Sec-"+name,gmap->first,"MTC",temp);
    }
    // CO2 emissions by Sector
    for (m=0;m<maxper;m++) {
        temp[m] = summary[m].get_emissmap_second("CO2");
    }
    dboutput4( regionName,"CO2 Emiss","by Sector",name,"MTC",temp);
    dboutput4( regionName,"CO2 Emiss",name,"zTotal","MTC",temp);

    // CO2 indirect emissions by Sector
    for (m=0;m<maxper;m++) {
        temp[m] = summary[m].get_emindmap_second("CO2");
    }
    dboutput4( regionName,"CO2 Emiss(ind)",name,"zTotal","MTC",temp);

    // Sector price
    dboutput4( regionName,"Price",name,"zSectorAvg","$/GJ",sectorprice);
    // for electricity Sector only
    if (name == "electricity") {
        for (m=0;m<maxper;m++) {
            temp[m] = sectorprice[m] * 2.212 * 0.36;
        }
        dboutput4( regionName,"Price","electricity C/kWh","zSectorAvg","90C/kWh",temp);
    }

    // Sector price
    dboutput4( regionName,"Price","by Sector",name,"$/GJ",sectorprice);
    // Sector carbon taxes paid
    for( int per = 0; per < maxper; ++per ){
        temp[ per ] = getTotalCarbonTaxPaid( per );
    }
    dboutput4( regionName,"General","CarbonTaxPaid",name,"$", temp );
    // do for all subsectors in the Sector
    for (int i=0;i<nosubsec;i++) {
        // output or demand for each technology
        subsec[ i ]->MCoutputSupplySector();
        subsec[ i ]->MCoutputAllSectors();
    }
}

//! Write subsector output to database.
void Sector::subsec_outfile() const {
    // do for all subsectors in the Sector
    for (int i=0;i<nosubsec;i++) {
        // output or demand for each technology
        subsec[ i ]->csvOutputFile();
    }
}

/*! \brief Returns total carbon tax paid by Sector.
*
* \author Sonny Kim
* \param period Model period
* \return total carbon taxes paid
*/
double Sector::getTotalCarbonTaxPaid( const int period ) const {
    double sum = 0;
    for( vector<Subsector*>::const_iterator currSub = subsec.begin(); currSub != subsec.end(); ++currSub ){
        sum += (*currSub)->getTotalCarbonTaxPaid( period );
    }
    return sum;
}

/*! \brief Return fuel consumption map for this Sector
*
* \author Sonny Kim
* \param period Model period
* \todo Input change name of this and other methods here to proper capitilization
* \return fuel consumption map
*/
map<string, double> Sector::getfuelcons( const int period ) const {
    return summary[ period ].getfuelcons();
}

//!  Get the second fuel consumption map in summary object.
/*! \brief Return fuel consumption for the specifed fuel
*
* \author Sonny Kim
* \param period Model period
* \param fuelName name of fuel
* \return fuel consumption
*/
double Sector::getConsByFuel( const int period, const std::string& fuelName ) const {
    return summary[ period ].get_fmap_second( fuelName );
}

/*! \brief Clear fuel consumption map for this Sector
*
* \author Sonny Kim
* \param period Model period
*/
void Sector::clearfuelcons( const int period ) {
    summary[ period ].clearfuelcons();
}

/*! \brief Return the ghg emissions map for this Sector
*
* \author Sonny Kim
* \param period Model period
* \return GHG emissions map
*/
map<string, double> Sector::getemission( const int period ) const {
    return summary[ period ].getemission();
}

/*! \brief Return ghg emissions map in summary object
*
* This map is used to calculate the emissions coefficient for this Sector (and fuel?) in region
*
* \author Sonny Kim
* \param period Model period
* \return GHG emissions map
*/
map<string, double> Sector::getemfuelmap( const int period ) const {
    return summary[ period ].getemfuelmap();
}

/*! \brief update summaries for reporting
*
*  Updates summary information for the Sector and all subsectors.
*
* \author Sonny Kim
* \param period Model period
* \return GHG emissions map
*/
void Sector::updateSummary( const int period ) {
    // clears Sector fuel consumption map
    summary[ period ].clearfuelcons();

    for ( int i = 0; i < nosubsec; i++ ) {
        // call update summary for subsector
        subsec[ i ]->updateSummary( period );
        // sum subsector fuel consumption for Sector fuel consumption
        summary[ period ].updatefuelcons(subsec[ i ]->getfuelcons( period )); 
    }
    // set input to total fuel consumed by Sector
    // input in Sector is used for reporting purposes only
    /*!\todo Something is very wrong here, input is calculated two very different ways */
    input[ period ] = summary[ period ].get_fmap_second("zTotal");
}

/*! \brief A function to add the sectors fuel dependency information to an existing graph.
*
* This function prints the sectors fuel dependencies to an existing graph.
*
* \author Josh Lurz
* \param outStream An output stream to write to which was previously created.
* \param period The period to print graphs for.
*/
void Sector::addToDependencyGraph( ostream& outStream, const int period ) const {
    const double DISPLAY_THRESHOLD = 0.00001; // Do not show links with values below this.
    const int DISPLAY_PRECISION = 2; // Number of digits to print of the value on the graph.
    
    // Values at which to switch the type of line used to display the link.
    const double DOTTED_LEVEL = 1.0;
    const double DASHED_LEVEL = 5.0;
    const double LINE_LEVEL = 10.0;
    
    // Make sure the outputstream is open.
    assert( outStream );
    
    // Get the supply Sector name and replace spaces in it with underscores. 
    string sectorName = getName();
    util::replaceSpaces( sectorName );

    // Print out the style for the Sector.
    printStyle( outStream );
    
    // Set whether to print prices or quantities on the graph. 
    const Configuration* conf = Configuration::getInstance();
    const Marketplace* marketplace = 0;
    const bool printPrices = conf->getBool( "PrintPrices", 0 );
    if( printPrices ){
        marketplace = scenario->getMarketplace();
    }

    // Now loop through the fuel map.
    typedef map<string,double>:: const_iterator CI;
    map<string, double> sectorsUsed = getfuelcons( period );
    string fuelName;

    for( CI fuelIter = sectorsUsed.begin(); fuelIter != sectorsUsed.end(); fuelIter++ ) {
        fuelName = fuelIter->first;
        
        // Skip zTotal
        if( fuelName == "zTotal" ){
            continue;
        }

        // Initialize the value of the line to a price or quantity.
        double graphValue = 0;
        if( printPrices ){
            graphValue = marketplace->getPrice( fuelIter->first, regionName, period );
        } else {
            graphValue = fuelIter->second;
        }

        if( ( graphValue >  DISPLAY_THRESHOLD ) || conf->getBool( "ShowNullPaths", 0 ) ) {
            util::replaceSpaces( fuelName );
            outStream << "\t" << fuelName << " -> " << sectorName;
            outStream << " [style=\"";
            if( graphValue < DOTTED_LEVEL ) {
                outStream << "dotted";
            }
            else if ( graphValue < DASHED_LEVEL ) {
                outStream << "dashed";
            }
            else if ( graphValue < LINE_LEVEL ) {
                outStream << "";
            }
            else {
                outStream << "bold";
            }

            outStream << "\"";
            
            if( conf->getBool( "PrintValuesOnGraphs" ) ) {
                outStream << ",label=\"";
                outStream << setiosflags( ios::fixed | ios::showpoint ) << setprecision( DISPLAY_PRECISION );
                outStream << graphValue;
                outStream << "\"";
            }
            outStream << "];" << endl;
        }
    }
}

/*! \brief A function to add the Sector coloring and style to the dependency graph.
*
* This function add the Sector specific coloring and style to the dependency graph.
*
* \author Josh Lurz
* \param outStream An output stream to write to which was previously created.
*/
void Sector::printStyle( ostream& outStream ) const {

    // Make sure the output stream is open.
    assert( outStream );

    // Get the Sector name.
    string sectorName = getName();
    util::replaceSpaces( sectorName );

    // output Sector coloring here.
}

/*! \brief A function to add the name of a Sector the current Sector has a simul with. 
*
* This function adds the name of the Sector to the simulList vector, if the name
* does not already exist within the vector. This vector is 
* then used to sort the sectors by fuel dependencies so that calculations are always 
* consistent. 
*
* \author Josh Lurz
* \param sectorName The name of the Sector the current Sector has a simul with. 
*/
void Sector::addSimul( const string sectorName ) {
    if( std::find( simulList.begin(), simulList.end(), sectorName ) == simulList.end() ) {
        simulList.push_back( sectorName );
    }
}

/*! \brief This function sets up the Sector for sorting. 
*
* This function uses the recursive function getInputDependencies to 
* find the full list of dependencies for the Sector, including 
* transative dependencies. It then sorts that list of dependencies
* for rapid searching.
*
* \author Josh Lurz
* \param parentRegion A pointer to the parent region.
*/
void Sector::setupForSort( const Region* parentRegion ) {

    // Setup the internal dependencies vector.
    dependsList = getInputDependencies( parentRegion );

    // Now sort the list.
    sort( dependsList.begin(), dependsList.end() );
}

/*! \brief This gets the full list of input dependencies including transative dependencies. 
*
* This function recursively determines the input dependencies for the Sector. To do this
* correctly, it must also recursively find all the input dependencies for its direct inputs.
* This can result in a long list of dependencies. Dependencies already accounted for by simuls
* are not included in this list. 
*
* \author Josh Lurz
* \param parentRegion A pointer to the parent region.
* \return The full list of input dependencies including transative ones. 
*/
vector<string> Sector::getInputDependencies( const Region* parentRegion ) const {
    // Setup the vector we will return.
    vector<string> depVector;

    // Setup an input vector.
    map<string,double> tempMap = getfuelcons( 0 );

    for( map<string, double>::const_iterator fuelIter = tempMap.begin(); fuelIter != tempMap.end(); fuelIter++ ) {
        string depSectorName = fuelIter->first;

        // Check for zTotal, which is not a Sector name and simuls which are not dependencies. 
        if( depSectorName != "zTotal" && ( find( simulList.begin(), simulList.end(), depSectorName ) == simulList.end() ) ) {
            // First add the dependency.
            depVector.push_back( depSectorName );

            // Now get the Sector's dependencies.
            vector<string> tempDepVector = parentRegion->getSectorDependencies( depSectorName );

            // Add the dependencies if they are unique.
            for( vector<string>::const_iterator tempVecIter = tempDepVector.begin(); tempVecIter != tempDepVector.end(); tempVecIter++ ) {
                // If the Sector is not already in the dep vector, add it. 
                if( find( depVector.begin(), depVector.end(), *tempVecIter ) == depVector.end() ){
                    depVector.push_back( *tempVecIter );
                }
            }
        }
    } // End input list loop
    // Return the list of dependencies. 
    return depVector;
}

/*! This function returns a copy of the list of dependencies of the Sector
*
* This function returns a vector of strings created during setupForSort.
* It lists the names of all inputs the Sector uses. These inputs are also sectors. 
*
* \author Josh Lurz
* \return A vector of Sector names which are inputs the Sector uses. 
*/
const vector<string>& Sector::getDependsList() const {
    return dependsList;
}

/*! \brief A function to print a csv file including a Sector's name and all it's dependencies. 
* 
* \author Josh Lurz
* \pre setupForSort function has been called to initialize the dependsList. 
* \param aLog The to which to print the dependencies. 
*/
void Sector::printSectorDependencies( ILogger& aLog ) const {
    aLog << "," << name << ",";
    for( vector<string>::const_iterator depIter = dependsList.begin(); depIter != dependsList.end(); depIter++ ) {
        aLog << *depIter << ",";
    }
    aLog << endl;
}


