/***************************************************************************
  file : $URL$
  version : $LastChangedRevision$  $LastChangedBy$
  date : $LastChangedDate$
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 * Copyright (C) 2007 by Johan De Taeye                                    *
 *                                                                         *
 * This library is free software; you can redistribute it and/or modify it *
 * under the terms of the GNU Lesser General Public License as published   *
 * by the Free Software Foundation; either version 2.1 of the License, or  *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This library is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser *
 * General Public License for more details.                                *
 *                                                                         *
 * You should have received a copy of the GNU Lesser General Public        *
 * License along with this library; if not, write to the Free Software     *
 * Foundation Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,*
 * USA                                                                     *
 *                                                                         *
 ***************************************************************************/

#define FREPPLE_CORE
#include "frepple/model.h"

namespace frepple
{

DECLARE_EXPORT const MetaCategory* LoadPlan::metadata;


int LoadPlan::initialize()
{
  // Initialize the metadata
  metadata = new MetaCategory("loadplan", "loadplans");

  // Initialize the Python type
  PythonType& x = FreppleCategory<LoadPlan>::getType();
  x.setName("loadplan");
  x.setDoc("frePPLe loadplan");
  x.supportgetattro();
  const_cast<MetaCategory*>(metadata)->pythonClass = x.type_object();
  return x.typeReady();
}


DECLARE_EXPORT LoadPlan::LoadPlan(OperationPlan *o, const Load *r)
{
  assert(o);
  ld = const_cast<Load*>(r);
  oper = o;
  start_or_end = START;

  // Add to the operationplan
  nextLoadPlan = NULL;
  if (o->firstloadplan)
  {
    // Append to the end
    LoadPlan *c = o->firstloadplan;
    while (c->nextLoadPlan) c = c->nextLoadPlan;
    c->nextLoadPlan = this;
  }
  else
    // First in the list
    o->firstloadplan = this;

  // Insert in the resource timeline
  r->getResource()->loadplans.insert(
    this,
    ld->getLoadplanQuantity(this),
    ld->getLoadplanDate(this)
    );

  // Initialize the Python type
  initType(metadata);

  // Create a loadplan to mark the end of the operationplan.
  new LoadPlan(o, r, this);

  // Mark the operation and resource as being changed. This will trigger
  // the recomputation of their problems
  r->getResource()->setChanged();
  r->getOperation()->setChanged();
}


DECLARE_EXPORT LoadPlan::LoadPlan(OperationPlan *o, const Load *r, LoadPlan *lp)
{
  ld = const_cast<Load*>(r);
  oper = o;
  start_or_end = END;

  // Add to the operationplan
  nextLoadPlan = NULL;
  if (o->firstloadplan)
  {
    // Append to the end
    LoadPlan *c = o->firstloadplan;
    while (c->nextLoadPlan) c = c->nextLoadPlan;
    c->nextLoadPlan = this;
  }
  else
    // First in the list
    o->firstloadplan = this;

  // Insert in the resource timeline
  r->getResource()->loadplans.insert(
    this,
    ld->getLoadplanQuantity(this),
    ld->getLoadplanDate(this)
    );

  // Initialize the Python type
  initType(metadata);
}


DECLARE_EXPORT LoadPlan* LoadPlan::getOtherLoadPlan() const
{
  for (LoadPlan *i = oper->firstloadplan; i; i = i->nextLoadPlan)
    if (i->ld == ld && i != this) return i;
  throw LogicException("No matching loadplan found");
}


DECLARE_EXPORT void LoadPlan::update()
{
  // Update the timeline data structure
  ld->getResource()->getLoadPlans().update(
    this,
    ld->getLoadplanQuantity(this),
    ld->getLoadplanDate(this)
    );

  // Review adjacent setups
  if (!isStart()) ld->getResource()->updateSetups(this);

  // Mark the operation and resource as being changed. This will trigger
  // the recomputation of their problems
  ld->getResource()->setChanged();
  ld->getOperation()->setChanged();
}


DECLARE_EXPORT const string& LoadPlan::getSetup(bool current) const
{
  // This resource has no setupmatrix
  static string nosetup;
  assert(ld);
  if (!ld->getResource()->getSetupMatrix()) return nosetup;

  // Current load has a setup
  if (!ld->getSetup().empty() && current) return ld->getSetup();

  // Scan earlier setups
  for (Resource::loadplanlist::const_iterator i(this); 
    i != getResource()->getLoadPlans().end(); --i)
  {
    const LoadPlan* j = dynamic_cast<const LoadPlan*>(&*i);
    if (j && !j->getLoad()->getSetup().empty() && (current || j != this))
      return j->getLoad()->getSetup();
  }

  // No conversions found - return the original setup
  return ld->getResource()->getSetup();
}


DECLARE_EXPORT void LoadPlan::setLoad(const Load* newld)
{
  // No change
  if (newld == ld) return;

  // Verify the data
  if (!newld) throw LogicException("Can't switch to NULL load");
  if (ld && ld->getOperation() != newld->getOperation())
    throw LogicException("Only switching to a load on the same operation is allowed");

  // Mark entities as changed
  if (oper) oper->getOperation()->setChanged();
  if (ld) ld->getResource()->setChanged();
  newld->getResource()->setChanged();

  // Update also the setup operationplan
  if (oper && oper->getOperation() != OperationSetup::setupoperation)
  {
    bool oldHasSetup = ld && !ld->getSetup().empty() 
      && ld->getResource()->getSetupMatrix();
    bool newHasSetup = !newld->getSetup().empty() 
      && newld->getResource()->getSetupMatrix();
    OperationPlan *setupOpplan = NULL;
    if (oldHasSetup)
    {
      for (OperationPlan::iterator i(oper); i != oper->end(); ++i)
        if (i->getOperation() == OperationSetup::setupoperation)
        {
          setupOpplan = &*i;
          break;
        }
      if (!setupOpplan) oldHasSetup = false;
    }
    if (oldHasSetup)
    {
      if (newHasSetup)
      {
        // Case 1: Both the old and new load require a setup
        LoadPlan *setupLdplan = NULL;
        for (OperationPlan::LoadPlanIterator j = setupOpplan->beginLoadPlans();
          j != setupOpplan->endLoadPlans(); ++j)
          if (j->getLoad() == ld)
          {
            setupLdplan = &*j;
            break;
          }
        if (!setupLdplan)
          throw LogicException("Can't find loadplan on setup operationplan");
        // Update the loadplan
        setupLdplan->setLoad(newld);
        setupOpplan->setEnd(setupOpplan->getDates().getEnd());
      }
      else
      {
        // Case 2: Delete the old setup which is not required any more
        oper->eraseSubOperationPlan(setupOpplan);
      }
    }
    else
    {
      if (newHasSetup)
      {
        // Case 3: Create a new setup operationplan
        OperationSetup::setupoperation->createOperationPlan(
          1, Date::infinitePast, oper->getDates().getEnd(), NULL, oper);
      }
      //else: 
      // Case 4: No setup for the old or new load
    }
  }

  // Change this loadplan and its brother
  for (LoadPlan *ldplan = getOtherLoadPlan(); true; )
  {
   // Remove from the old resource, if there is one
    if (ldplan->ld)
      ldplan->ld->getResource()->loadplans.erase(ldplan);
    
    // Update the setups on the old resource
    if (ldplan == this && ldplan->getOperationPlan()->getOperation() == OperationSetup::setupoperation)
      ld->getResource()->updateSetups();

    // Insert in the new resource
    ldplan->ld = newld;
    newld->getResource()->loadplans.insert(
      ldplan,
      newld->getLoadplanQuantity(ldplan),
      newld->getLoadplanDate(ldplan)
      );

    // Repeat for the brother loadplan or exit
    if (ldplan != this) ldplan = this;
    else return;
  }
}


PyObject* LoadPlan::getattro(const Attribute& attr)
{
  if (attr.isA(Tags::tag_operationplan))
    return PythonObject(getOperationPlan());
  if (attr.isA(Tags::tag_quantity))
    return PythonObject(getQuantity());
  if (attr.isA(Tags::tag_startdate))
    return PythonObject(getDate());
  if (attr.isA(Tags::tag_enddate))
    return PythonObject(getOtherLoadPlan()->getDate());
  if (attr.isA(Tags::tag_resource))
    return PythonObject(getLoad()->getResource());
  if (attr.isA(Tags::tag_setup))
    return PythonObject(getSetup());
  return NULL;
}


int LoadPlanIterator::initialize()
{
  // Initialize the type
  PythonType& x = PythonExtension<LoadPlanIterator>::getType();
  x.setName("loadplanIterator");
  x.setDoc("frePPLe iterator for loadplan");
  x.supportiter();
  return x.typeReady();
}


PyObject* LoadPlanIterator::iternext()
{
  LoadPlan* ld;
  if (resource_or_opplan)
  {
    // Skip zero quantity loadplans and load ends
    while (*resiter != res->getLoadPlans().end() && (*resiter)->getQuantity()<=0.0)
      ++(*resiter);
    if (*resiter == res->getLoadPlans().end()) return NULL;

    // Return result
    ld = const_cast<LoadPlan*>(static_cast<const LoadPlan*>(&*((*resiter)++)));
  }
  else
  {
    while (*opplaniter != opplan->endLoadPlans() && (*opplaniter)->getQuantity()==0.0)
      ++(*opplaniter);
    if (*opplaniter == opplan->endLoadPlans()) return NULL;
    ld = static_cast<LoadPlan*>(&*((*opplaniter)++));
  }
  Py_INCREF(ld);
  return const_cast<LoadPlan*>(ld);
}

} // end namespace
