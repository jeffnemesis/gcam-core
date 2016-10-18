#ifndef _REGION_MINICAM_H_
#define _REGION_MINICAM_H_
#if defined(_MSC_VER)
#pragma once
#endif

/*
* LEGAL NOTICE
* This computer software was prepared by Battelle Memorial Institute,
* hereinafter the Contractor, under Contract No. DE-AC05-76RL0 1830
* with the Department of Energy (DOE). NEITHER THE GOVERNMENT NOR THE
* CONTRACTOR MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR ASSUMES ANY
* LIABILITY FOR THE USE OF THIS SOFTWARE. This notice including this
* sentence must appear on any copies of this computer software.
* 
* EXPORT CONTROL
* User agrees that the Software will not be shipped, transferred or
* exported into any country or used in any manner prohibited by the
* United States Export Administration Act or any other applicable
* export laws, restrictions or regulations (collectively the "Export Laws").
* Export of the Software may require some form of license or other
* authority from the U.S. Government, and failure to obtain such
* export control license may result in criminal liability under
* U.S. laws. In addition, if the Software is identified as export controlled
* items under the Export Laws, User represents and warrants that User
* is not a citizen, or otherwise located within, an embargoed nation
* (including without limitation Iran, Syria, Sudan, Cuba, and North Korea)
*     and that User is not otherwise prohibited
* under the Export Laws from receiving the Software.
* 
* Copyright 2011 Battelle Memorial Institute.  All Rights Reserved.
* Distributed as open-source under the terms of the Educational Community 
* License version 2.0 (ECL 2.0). http://www.opensource.org/licenses/ecl2.php
* 
* For further details, see: http://www.globalchange.umd.edu/models/gcam/
*
*/



/*!
* \file region_minicam.h
* \ingroup Objects
* \brief The RegionMiniCAM class header file.
* \author Sonny Kim
*/

#include <map>
#include <vector>
#include <memory>
#include <string>
#include <list>
#include <xercesc/dom/DOMNode.hpp>

#include "containers/include/region.h"
#include "util/base/include/ivisitable.h"
#include "util/base/include/iround_trippable.h"
#include "util/base/include/value.h"

// Forward declarations.
class Population;
class Demographic;
class Sector;
class SupplySector;
class ILandAllocator;
class GHGPolicy;
class Summary;
class ILogger;
class GDP;
class Curve;
class AFinalDemand;
class Consumer;

/*! 
* \ingroup Objects
* \brief This class defines a single region of the model and contains other
*        regional information such as demographics, resources, supply and demand
*        sectors, and GDPs. The classes contained in the Region are the
*        Populations, Resource, Sector, FinalDemands.  Since this particular
*        implementation of the model is based on a partial equilibrium concept,
*        it is not mandatory to instantiate all of these classes.  The region
*        can contain just one of these objects or any combination of each of
*        these objects.  The demand sector object, however, requires Populations
*        information to drive the demand for goods and services. The Region class also
*        contains the GhgMarket class which is instantiated only when a market
*        for ghg emissions is needed. Due to inter-dependencies between regions
*        the calculations of the sectors, resources, etc are performed at the
*        world level to ensure the correct calculation order.
*
* \author Sonny Kim
*/

class RegionMiniCAM: public Region
{
    friend class InputOutputTable;
    friend class SocialAccountingMatrix;
    friend class DemandComponentsTable;
    friend class SectorReport;
    friend class SGMGenTable;
    friend class XMLDBOutputter;
public:
    RegionMiniCAM();
    virtual ~RegionMiniCAM();
    static const std::string& getXMLNameStatic();
    virtual void completeInit();
    
    virtual void initCalc( const int period );

    virtual void postCalc( const int aPeriod );

    virtual void csvOutputFile() const;
    virtual void dbOutput( const std::list<std::string>& aPrimaryFuelList ) const;
    virtual void updateSummary( const std::list<std::string>& aPrimaryFuelList, const int period );
    virtual const Summary& getSummary( const int period ) const;

    virtual bool isAllCalibrated( const int period, double calAccuracy, const bool printWarnings ) const;
    virtual void updateAllOutputContainers( const int period );
    virtual void accept( IVisitor* aVisitor, const int aPeriod ) const;
protected:
    std::auto_ptr<GDP> gdp; //!< GDP object.

    //! Regional land allocator.
    std::auto_ptr<ILandAllocator> mLandAllocator;

    std::vector<AFinalDemand*> mFinalDemands; //!< vector of pointers to demand sector objects

    std::vector<double> calibrationGDPs; //!< GDPs to calibrate to
    std::vector<double> GDPcalPerCapita; //!< GDP per capita to calibrate to

    //! GCAM consumers
    std::vector<Consumer*> mConsumers;

    std::vector<Summary> summary; //!< summary values and totals for reporting
    std::map<std::string,int> supplySectorNameMap; //!< Map of supplysector name to integer position in vector.
    std::map<std::string, double> primaryFuelCO2Coef; //!< map of CO2 emissions coefficient for primary fuels only

    //! Interest rate for the region.
    double mInterestRate;

    virtual const std::string& getXMLName() const;
    virtual void toInputXMLDerived( std::ostream& out, Tabs* tabs ) const;
    virtual bool XMLDerivedClassParse( const std::string& nodeName, const xercesc::DOMNode* curr );
    virtual void toDebugXMLDerived( const int period, std::ostream& out, Tabs* tabs ) const;

    void initElementalMembers();

    void calcGDP( const int period );
    double getEndUseServicePrice( const int period ) const;
    void adjustGDP( const int period );

    const std::vector<double> calcFutureGDP() const;
    void calcEmissions( const int period );
    void calcEmissFuel( const std::list<std::string>& aPrimaryFuelList, const int period );

    void setCO2CoefsIntoMarketplace( const int aPeriod );

private:
    void clear();
    bool ensureGDP() const;
    bool ensureDemographics() const;
};

#endif // _REGION_H_
