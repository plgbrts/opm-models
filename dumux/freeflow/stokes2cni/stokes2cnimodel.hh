/*****************************************************************************
 *   Copyright (C) 2010 by Klaus Mosthaf                                     *
 *   Copyright (C) 2008-2009 by Bernd Flemisch, Andreas Lauser               *
 *   Institute of Hydraulic Engineering                                      *
 *   University of Stuttgart, Germany                                        *
 *   email: <givenname>.<name>@iws.uni-stuttgart.de                          *
 *                                                                           *
 *   This program is free software: you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation, either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 *****************************************************************************/
/*!
 * \file
 *
 * \brief Adaption of the BOX scheme to the non-isothermal
 *        compositional stokes model (with two components).
 */
#ifndef DUMUX_STOKES2CNI_MODEL_HH
#define DUMUX_STOKES2CNI_MODEL_HH

#include <dumux/freeflow/stokes2c/stokes2cmodel.hh>

#include "stokes2cnilocalresidual.hh"
#include "stokes2cniproperties.hh"
#include "stokes2cniproblem.hh"

namespace Dumux {

/*!
 * \ingroup BoxProblems
 * \defgroup Stokes2cniBoxProblems Non-isothermal compositional stokes problems
 */

/*!
 * \ingroup BoxModels
 * \defgroup Stokes2cniModel Non-isothermal compositional box stokes model
 */

/*!
 * \ingroup Stokes2cniModel
 * \brief Adaption of the BOX scheme to the non-isothermal compositional stokes model.
 *
 * This model implements a non-isothermal flow of a fluid
 * \f$\alpha \in \{ w, n \}\f$.
 * Using the standard Stokes approach a mass balance equation is
 * solved:
 * \f{eqnarray*}
 && \phi \frac{\partial (\sum_\alpha \varrho_\alpha X_\alpha^\kappa S_\alpha )}{\partial t}
 - \sum_\alpha \text{div} \left\{ \varrho_\alpha X_\alpha^\kappa
 \frac{k_{r\alpha}}{\mu_\alpha} \mbox{\bf K}
 (\text{grad} p_\alpha - \varrho_{\alpha} \mbox{\bf g}) \right\}\\
 &-& \sum_\alpha \text{div} \left\{{\bf D_{\alpha, pm}^\kappa} \varrho_{\alpha} \text{grad} X^\kappa_{\alpha} \right\}
 - \sum_\alpha q_\alpha^\kappa = \quad 0 \qquad \kappa \in \{w, a\} \, ,
 \alpha \in \{w, n\}
 *     \f}
 \f}
 *
 * This is discretized using a fully-coupled vertex
 * centered finite volume (box) scheme as spatial and
 * the implicit Euler method as temporal discretization.
 *
 */
template<class TypeTag>
class Stokes2cniModel : public Stokes2cModel<TypeTag>
{
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef typename GET_PROP_TYPE(TypeTag, GridView) GridView;
    typedef typename GET_PROP_TYPE(TypeTag, Stokes2cniIndices) Indices;

    enum {
        dim = GridView::dimension,
        dimWorld = GridView::dimensionworld
    };
    enum { numEq = GET_PROP_VALUE(TypeTag, NumEq) };
    enum {
        lCompIdx = Indices::lCompIdx,
        gCompIdx = Indices::gCompIdx
    };
    enum { phaseIdx = GET_PROP_VALUE(TypeTag, PhaseIndex) };

    typedef typename GridView::template Codim<0>::Iterator ElementIterator;

    typedef typename GET_PROP_TYPE(TypeTag, FVElementGeometry) FVElementGeometry;
    typedef typename GET_PROP_TYPE(TypeTag, ElementBoundaryTypes) ElementBoundaryTypes;
    typedef typename GET_PROP_TYPE(TypeTag, SolutionVector) SolutionVector;

    typedef typename GET_PROP_TYPE(TypeTag, VolumeVariables) VolumeVariables;

public:
    /*!
     * \brief Append all quantities of interest which can be derived
     *        from the solution of the current time step to the VTK
     *        writer.
     */
    template <class MultiWriter>
    void addOutputVtkFields(const SolutionVector &sol, MultiWriter &writer)
    {
        typedef Dune::BlockVector<Dune::FieldVector<Scalar, 1> > ScalarField;
        typedef Dune::BlockVector<Dune::FieldVector<Scalar, dim> > VelocityField;

        // create the required scalar fields
        unsigned numVertices = this->gridView_().size(dim);
        ScalarField &pN = *writer.allocateManagedBuffer(numVertices);
        ScalarField &delP = *writer.allocateManagedBuffer(numVertices);
        ScalarField &Xw = *writer.allocateManagedBuffer(numVertices);
        ScalarField &T = *writer.allocateManagedBuffer(numVertices);
        ScalarField &rho = *writer.allocateManagedBuffer(numVertices);
        ScalarField &mu = *writer.allocateManagedBuffer(numVertices);
        ScalarField &h = *writer.allocateManagedBuffer(numVertices);
//        ScalarField &D = *writer.allocateManagedBuffer(numVertices);
        VelocityField &velocity = *writer.template allocateManagedBuffer<Scalar, dim> (numVertices);

        unsigned numElements = this->gridView_().size(0);
        ScalarField &rank = *writer.allocateManagedBuffer(numElements);

        FVElementGeometry fvElemGeom;
        VolumeVariables volVars;
        ElementBoundaryTypes elemBcTypes;

        ElementIterator elemIt = this->gridView_().template begin<0>();
        ElementIterator endit = this->gridView_().template end<0>();
        for (; elemIt != endit; ++elemIt)
        {
            int idx = this->elementMapper().map(*elemIt);
            rank[idx] = this->gridView_().comm().rank();

            fvElemGeom.update(this->gridView_(), *elemIt);
            elemBcTypes.update(this->problem_(), *elemIt, fvElemGeom);

            int numLocalVerts = elemIt->template count<dim>();
            for (int vertexIdx = 0; vertexIdx < numLocalVerts; ++vertexIdx)
            {
                int globalIdx = this->vertexMapper().map(*elemIt, vertexIdx, dim);
                volVars.update(sol[globalIdx],
                               this->problem_(),
                               *elemIt,
                               fvElemGeom,
                               vertexIdx,
                               false);

                pN  [globalIdx] = volVars.pressure();
                delP[globalIdx] = volVars.pressure() - 1e5;
                Xw  [globalIdx] = volVars.fluidState().massFraction(phaseIdx, lCompIdx);
                T   [globalIdx] = volVars.temperature();
                rho [globalIdx] = volVars.density();
                mu  [globalIdx] = volVars.viscosity();
                h   [globalIdx] = volVars.enthalpy();
//                D   [globalIdx] = volVars.diffusionCoeff();
                velocity[globalIdx] = volVars.velocity();
            };
        }
        writer.attachVertexData(pN, "pg");
        writer.attachVertexData(delP, "delP");
//        writer.attachVertexData(D, "Dwg");
        writer.attachVertexData(Xw, "X_gH2O");
        writer.attachVertexData(T, "temperature");
        writer.attachVertexData(rho, "rhoG");
        writer.attachVertexData(mu, "mu");
        writer.attachVertexData(h, "h");
        writer.attachVertexData(velocity, "v", dim);
    }
};

}
#endif
