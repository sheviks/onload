/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
#include <linux/module.h>
#include <linux/netdevice.h>
#include "net_driver.h"
#include "efx.h"
#include "mcdi_port_common.h"
#include "ethtool_common.h"
#include "ef100_ethtool.h"
#include "mcdi_functions.h"

/* This is the maximum number of descriptor rings supported by the QDMA */
#define EFX_EF100_MAX_DMAQ_SIZE 16384UL

static void ef100_ethtool_get_ringparam(struct net_device *net_dev,
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_ETHTOOL_GET_RINGPARAM_EXTACK)
					struct ethtool_ringparam *ring,
				       struct kernel_ethtool_ringparam *kring,
					struct netlink_ext_ack *ext_ack)
#else
					struct ethtool_ringparam *ring)
#endif
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	unsigned long driver_bitmap;
	unsigned long max_size = 0;

	driver_bitmap = EFX_EF100_MAX_DMAQ_SIZE | (EFX_EF100_MAX_DMAQ_SIZE - 1);
	driver_bitmap &= efx->guaranteed_bitmap;
	if (driver_bitmap)
		max_size = rounddown_pow_of_two(driver_bitmap);

	ring->rx_max_pending = max_size;
	ring->tx_max_pending = max_size;
	ring->rx_pending = efx->rxq_entries;
	ring->tx_pending = efx->txq_entries;
}

static int ef100_ethtool_set_ringparam(struct net_device *net_dev,
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_ETHTOOL_SET_RINGPARAM_EXTACK)
				       struct ethtool_ringparam *ring,
				       struct kernel_ethtool_ringparam *kring,
				       struct netlink_ext_ack *ext_ack)
#else
				       struct ethtool_ringparam *ring)
#endif
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	bool is_up = !efx_check_disabled(efx) && netif_running(efx->net_dev);
	int rc = 0;

	if (ring->rx_mini_pending || ring->rx_jumbo_pending)
		return -EINVAL;

	if (!is_power_of_2(ring->rx_pending) ||
	    !is_power_of_2(ring->tx_pending)) {
		netif_err(efx, drv, efx->net_dev,
			  "ring sizes that are not pow of 2, not supported");
		return -EINVAL;
	}
	if (ring->rx_pending == efx->rxq_entries &&
	    ring->tx_pending == efx->txq_entries)
		/* Nothing to do */
		return 0;

	if (!efx->supported_bitmap) {
		netif_err(efx, drv, efx->net_dev,
			  "ring size changes not supported\n");
		return -EOPNOTSUPP;
	}
	if (ring->rx_pending &&
	    !(efx->guaranteed_bitmap & ring->rx_pending)) {
		netif_err(efx, drv, efx->net_dev,
			  "unsupported ring size for RX");
		return -ERANGE;
	}
	if (ring->tx_pending &&
	    !(efx->guaranteed_bitmap & ring->tx_pending)) {
		netif_err(efx, drv, efx->net_dev,
			  "unsupported ring sizes for TX");
		return -ERANGE;
	}

#ifdef EFX_NOT_UPSTREAM
#if IS_MODULE(CONFIG_SFC_DRIVERLINK) || defined(CONFIG_AUXILIARY_BUS)
	if (efx->open_count > is_up) {
		netif_err(efx, drv, efx->net_dev,
			  "unable to set ring sizes. device in use by %hu clients\n",
			  efx->open_count);
		return -EBUSY;
	}
#endif
#endif

	/* Apply the new settings */
	efx->rxq_entries = ring->rx_pending;
	efx->txq_entries = ring->tx_pending;

	/* Update the datapath with the new settings if the interface is up */
	if (is_up) {
		dev_close(net_dev);
		rc = dev_open(net_dev, NULL);
	}

	return rc;
}

static void ef100_ethtool_get_drvinfo(struct net_device *net_dev,
				      struct ethtool_drvinfo *info)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);

	efx_ethtool_get_common_drvinfo(efx, info);
	if (!in_interrupt())
		efx_mcdi_print_fw_bundle_ver(efx, info->fw_version,
					     sizeof(info->fw_version));
}

/*	Ethtool options available
 */
const struct ethtool_ops ef100_ethtool_ops = {
#if !defined(EFX_USE_KCOMPAT) || (defined(EFX_HAVE_ETHTOOL_RXFH_PARAM) && defined(EFX_HAVE_CAP_RSS_CTX_SUPPORTED))
	.cap_rss_ctx_supported	= true,
#endif
	.get_drvinfo		= ef100_ethtool_get_drvinfo,
	.get_msglevel		= efx_ethtool_get_msglevel,
	.set_msglevel		= efx_ethtool_set_msglevel,
	.nway_reset		= efx_ethtool_nway_reset,
	.get_pauseparam         = efx_ethtool_get_pauseparam,
	.set_pauseparam         = efx_ethtool_set_pauseparam,
	.get_sset_count		= efx_ethtool_get_sset_count,
	.get_priv_flags		= efx_ethtool_get_priv_flags,
	.set_priv_flags		= efx_ethtool_set_priv_flags,
	.self_test		= efx_ethtool_self_test,
	.get_strings		= efx_ethtool_get_strings,
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_ETHTOOL_LINKSETTINGS)
	.get_link_ksettings	= efx_ethtool_get_link_ksettings,
	.set_link_ksettings	= efx_ethtool_set_link_ksettings,
#else
	.get_settings		= efx_ethtool_get_settings,
	.set_settings		= efx_ethtool_set_settings,
#endif
	.get_link		= ethtool_op_get_link,
	.get_ringparam		= ef100_ethtool_get_ringparam,
	.set_ringparam		= ef100_ethtool_set_ringparam,
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_ETHTOOL_FECPARAM)
	.get_fecparam		= efx_ethtool_get_fecparam,
	.set_fecparam		= efx_ethtool_set_fecparam,
#endif
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_ETHTOOL_SET_PHYS_ID)
	.set_phys_id            = efx_ethtool_phys_id,
#else
	.phys_id                = efx_ethtool_phys_id_loop,
#endif
	.get_ethtool_stats	= efx_ethtool_get_stats,
#if !defined(EFX_USE_KCOMPAT)
	.get_rxnfc              = efx_ethtool_get_rxnfc,
	.set_rxnfc              = efx_ethtool_set_rxnfc,
#else
	.get_rxnfc              = efx_ethtool_get_rxnfc_wrapper,
	.set_rxnfc              = efx_ethtool_set_rxnfc_wrapper,
#endif
#if defined(EFX_USE_KCOMPAT) && (!defined(EFX_USE_DEVLINK) || defined(EFX_NEED_ETHTOOL_FLASH_DEVICE))
	.flash_device		= efx_ethtool_flash_device,
#endif
	.reset                  = efx_ethtool_reset,
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_ETHTOOL_GET_RXFH_INDIR_SIZE)
	.get_rxfh_indir_size	= efx_ethtool_get_rxfh_indir_size,
#endif
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_ETHTOOL_GET_RXFH_KEY_SIZE)
	.get_rxfh_key_size	= efx_ethtool_get_rxfh_key_size,
#endif
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_CONFIGURABLE_RSS_HASH)
	.get_rxfh		= efx_ethtool_get_rxfh,
	.set_rxfh		= efx_ethtool_set_rxfh,
#elif defined(EFX_HAVE_ETHTOOL_GET_RXFH)
	.get_rxfh		= efx_ethtool_get_rxfh_no_hfunc,
	.set_rxfh		= efx_ethtool_set_rxfh_no_hfunc,
#elif defined(EFX_HAVE_ETHTOOL_GET_RXFH_INDIR)
	.get_rxfh_indir		= efx_ethtool_get_rxfh_indir,
	.set_rxfh_indir		= efx_ethtool_set_rxfh_indir,
#endif
#if defined(EFX_USE_KCOMPAT) && defined(EFX_HAVE_ETHTOOL_RXFH_CONTEXT)
	.get_rxfh_context	= efx_ethtool_get_rxfh_context,
	.set_rxfh_context	= efx_ethtool_set_rxfh_context,
#endif

#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_ETHTOOL_GMODULEEEPROM)
	.get_module_info	= efx_ethtool_get_module_info,
	.get_module_eeprom	= efx_ethtool_get_module_eeprom,
#endif

	.get_channels		= efx_ethtool_get_channels,
	.set_channels		= efx_ethtool_set_channels,
};

