<?php
/**
 * @author  Paul Trilsbeek, Ibrahim Abdullah
 * @package Flat License
 */

class LicenseExtractor
{
    /**
     * @var Apache_Solr_Service
     */
    private $solr;

    /**
     * Connect to apache solr
     *
     * @return Apache_Solr_Service
     */
    public function connect()
    {
        if (null !== $this->solr) {
            return $this->solr;
        }

        $path_parts = parse_url(variable_get('islandora_solr_url', 'localhost:8080/solr'));

        $this->solr = new Apache_Solr_Service($path_parts['host'], $path_parts['port'], $path_parts['path'] . '/');
        $this->solr->setCreateDocuments(0);

        return $this->solr;
    }

    /**
     * Common solr query performer
     *
     * @param string $query
     * @param array $params
     *
     * @return array
     */
    public function query($query, $params = [])
    {
        $this->connect();

        try {
            $results = $this->solr->search($query, 0, 1000, $params);
        } catch (Exception $e) {
            drupal_set_message(check_plain(t('Error searching Solr index')) . ' ' . $e->getMessage(), 'error');
            return false;
        }

        return json_decode($results->getRawResponse(), true);
    }

    /**
     * Get ancestors for given pid and return them including pid itself
     *
     * @param string $pid
     *
     * @return array
     */
    public function ancestors($pid)
    {
        $field = 'ancestors_ms';
        $solrParams = [];
        // Restrict results based on specified namespaces.
        $namespace_list = trim(variable_get('islandora_solr_namespace_restriction', ''));
        if ($namespace_list) {
            $namespaces = preg_split('/[,|\s]/', $namespace_list);
            $namespace_array = array();
            foreach (array_filter($namespaces) as $namespace) {
                $namespace_array[] = "PID:$namespace\:*";
            }
            $solrParams['fq'][] = implode(' OR ', $namespace_array);
        }

        $solrParams['fl'][] = $field;

        $data  = $this->query('PID:"' . $pid . '"', $solrParams);

        if (!array_key_exists($field, $data['response']['docs'][0])) {
            return;
        }

        $result = $data['response']['docs'][0][$field];

        if (!empty($result)) {
            //add pid itself as we also want info about it
            array_unshift($result, $pid);
        }

        return $result;
    }

    /**
     * Get info on each ancestor for building the citation
     *
     * @param array $ancestors
     *
     * @return array
     */
    public function ancestorInfo($ancestors)
    {
        $data     = $this->query($this->ancestorsQuery($ancestors), $this->ancestorsParams($ancestors));

        if ($data['response']['numFound'] === 0) {
            return false;
        }

        if (!isset($data['response']['docs'])) {
            return false;
        }

        return $data['response']['docs'];
    }

    /**
     * Build query for getting details for each ancestor
     *
     * @param array $ancestors
     *
     * @return string
     */
    public function ancestorsQuery($ancestors)
    {

        $query_values = [];

        if (!empty($ancestors)) {
            foreach ($ancestors as $ancestor) {
                $query_values[] = format_string('PID:("info:fedora/!ancestor_pid" OR "!ancestor_pid")', [
                    '!ancestor_pid' => $ancestor,
                ]);
            }
        }

        return implode(" OR ", $query_values);
    }

    /**
     * Query params (fields we want returned from Solr in order to evaluate which license is applied)
     *
     * @param string $parent
     *
     * @return array
     */
    public function ancestorsParams($parent)
    {

        $solrParams['fl'][] = 'PID,cmd.license,CMD_Components_MPI_Bundle_License_License_ms,
        CMD_Components_MPI_Bundle_License_LicenseURL_ms,policy_datastream_roles_ms,
        compound_policy_datastream_children_access_levels_ms,RELS_EXT_hasModel_uri_ms,
        RELS_EXT_isConstituentOf_uri_ms';

        return $solrParams;
    }

    /**
     * Find default configured licenses for a given ancestor tree
     *
     * @param array $ancestors
     * @param array $db_licenses
     *
     * @return array
     */
    public function getDefaultLicense($ancestors, $db_licenses)
    {

        $licenses = [];

        foreach ($ancestors as $ancestor) {
            $keys = array_search($ancestor, array_column($db_licenses, 'pid', 'id'));
            $keys = array_keys(array_column($db_licenses, 'pid', 'id'), $ancestor);

            if ($keys) {
                foreach ($keys as $key) {
                    $licenses[] = $db_licenses[$key];
                }
                break; // stop at the first one (lowest ancestor with a match)
            }
        }
        return $licenses;
    }

    /**
     * Look up license(s) to be used for a given node
     *
     * @param string $pid
     *
     * @return array|false
     */
    public function lookup($pid)
    {

        $db_licenses = flat_licenses_db_get_values();

        $license_labels = [];
        $license_urls = [];

        $type = "";

        $header = "<h2>License(s)/Agreement(s)</h2>";

        $no_license_message = "No License information available. Please contact the archive staff for advice.";

        $current_path = current_path();

        // hide license if not on object view page (path has additional segment)
        $hide = preg_match('/.*islandora\/object\/.+\/.+/', $current_path);

        if ($hide) {
            return "";
        }

        $ancestors = $this->ancestors($pid);

        if (!empty($ancestors)) {
            $info = $this->ancestorInfo($ancestors);

            if (empty($info)) {
                // root collection, no license shown
                return;
            }

            if (!empty($info)) {
                // See whether the current object is a Collection, Bundle or File,
                // and whether it has a CMDI content model
                $key = array_search($ancestors[0], array_column($info, 'PID'));

                if (array_key_exists('RELS_EXT_hasModel_uri_ms', $info[$key])) {
                    $models = $info[$key]['RELS_EXT_hasModel_uri_ms'];

                    $type = "file";

                    if (in_array("info:fedora/islandora:collectionCModel", $models)
                    and !in_array("info:fedora/islandora:sp_cmdiCModel", $models)) {
                        // object is a higher level collection that does not have CMDI metadata, no license shown
                        return;
                    }

                    if (in_array("info:fedora/islandora:collectionCModel", $models)) {
                        $type = "collection";
                    }

                    if (in_array("info:fedora/islandora:compoundCModel", $models)
                    and !in_array("info:fedora/islandora:collectionCModel", $models)) {
                        $type = "bundle";
                    }
                }

                $multiple_licenses_message = "different licences apply to different parts of this " . $type . ".";

                if ($type == "bundle") {
                    // get license details of bundle itself, if it has any
                    $key = array_search($ancestors[0], array_column($info, 'PID'));
                    $bundle_info = ($info[$key]);
                    if (array_key_exists('CMD_Components_MPI_Bundle_License_License_ms', $bundle_info)) {
                        $license_labels[] = $bundle_info['CMD_Components_MPI_Bundle_License_License_ms'];
                    }
                    if (array_key_exists('CMD_Components_MPI_Bundle_License_LicenseURL_ms', $bundle_info)) {
                        $license_urls[] = $bundle_info['CMD_Components_MPI_Bundle_License_LicenseURL_ms'];
                    }
                }

                if ($type == "file") {
                    // get license details of parent bundle, if it has any
                    $key = array_search($pid, array_column($info, 'PID'));

                    // we need a separate info query for parent bundle of the file, as that is not in ancestors but in
                    // "RELS_EXT_isConstituentOf_uri_ms".

                    if (array_key_exists('RELS_EXT_isConstituentOf_uri_ms', $info[$key])) {
                        $parent = $info[$key]['RELS_EXT_isConstituentOf_uri_ms'];

                        // get rid of info:fedora prefix
                        $parent = str_replace("info:fedora/", "", $parent);

                        $parentinfo = $this->ancestorInfo($parent);

                        $parentkey = array_search($parent, array_column($info, 'PID'));

                        if (array_key_exists('CMD_Components_MPI_Bundle_License_License_ms', $parentinfo[0])) {
                            $license_labels[] = $parentinfo[0]['CMD_Components_MPI_Bundle_License_License_ms'][0];
                        }
                        if (array_key_exists('CMD_Components_MPI_Bundle_License_LicenseURL_ms', $parentinfo[0])) {
                            $license_urls[] = $parentinfo[0]['CMD_Components_MPI_Bundle_License_LicenseURL_ms'][0];
                        }
                    }
                }

                if (($type == "bundle" or $type == "file") and (empty($license_labels) or empty($license_urls))) {
                    // in case no license info was found for the bundle or parent bundle,
                    // look up default configured licenses for the ancestor tree
                    $licenses = $this->getDefaultLicense($ancestors, $db_licenses);
                    foreach ($licenses as $license) {
                        $license_labels[] = $license['label'];
                        $license_urls[] = $license['url'];
                    }
                }

                if ($type == "collection") {
                    // find licenses with a facet query for everything within the selected collection

                    $query_values[] = format_string('ancestors_ms:("info:fedora/!pid" OR "!pid")', [
                        '!pid' => $pid,
                    ]);

                    $solrParams['fl'][] = 'PID';
                    $solrParams['facet'][] = 'true';
                    $solrParams['facet.mincount'][] = '1';
                    $solrParams['facet.field'][] = 'RELS_EXT_hasModel_uri_ms'; // to figure out how many bundles
                    $solrParams['facet.field'][] = 'cmd.license';
                    $solrParams['facet.field'][] = 'CMD_Components_MPI_Bundle_License_License_ms';
                    $solrParams['facet.field'][] = 'CMD_Components_MPI_Bundle_License_LicenseURL_ms';

                    $raw_result = $this->query($query_values, $solrParams);

                    // calculate how many bundles there are without their own license
                    $fedora_models = array_key_exists('RELS_EXT_hasModel_uri_ms', $raw_result['facet_counts']['facet_fields']) ?
                        $raw_result['facet_counts']['facet_fields']['RELS_EXT_hasModel_uri_ms'] : [];
                    $number_of_cmdi = array_key_exists('info:fedora/islandora:sp_cmdiCModel', $fedora_models) ?
                        $fedora_models['info:fedora/islandora:sp_cmdiCModel'] : 0;
                    $number_of_collections = array_key_exists('info:fedora/islandora:collectionCModel', $fedora_models) ?
                        $fedora_models['info:fedora/islandora:collectionCModel'] : 0;
                    $number_of_bundles = $number_of_cmdi - $number_of_collections;
                    $number_of_licensed_mpi_bundle = array_key_exists(
                        'CMD_Components_MPI_Bundle_License_License_ms',
                        $raw_result['facet_counts']['facet_fields']
                    )
                    ? array_sum($raw_result['facet_counts']['facet_fields']['CMD_Components_MPI_Bundle_License_License_ms']) : 0;
                    $number_of_licensed_other = array_key_exists('cmd.license', $raw_result['facet_counts']['facet_fields'])
                    ? array_sum($raw_result['facet_counts']['facet_fields']['cmd.license']) : 0;
                    $number_of_licensed_bundles = $number_of_licensed_mpi_bundle + $number_of_licensed_other;
                    $unlicenced_bundles = ($number_of_bundles > $number_of_licensed_bundles) ? true : false;

                    // licenses from the child bundles
                    $license_labels = array_key_exists('CMD_Components_MPI_Bundle_License_License_ms', $raw_result['facet_counts']['facet_fields']) ?
                        array_keys($raw_result['facet_counts']['facet_fields']['CMD_Components_MPI_Bundle_License_License_ms']) : [];
                    $license_urls = array_key_exists('CMD_Components_MPI_Bundle_License_LicenseURL_ms', $raw_result['facet_counts']['facet_fields']) ?
                        array_keys($raw_result['facet_counts']['facet_fields']['CMD_Components_MPI_Bundle_License_LicenseURL_ms']) : [];

                    if ($unlicenced_bundles or ($number_of_licensed_bundles == 0)) {
                        // also display any default licenses
                        $licenses = $this->getDefaultLicense($ancestors, $db_licenses);
                        foreach ($licenses as $license) {
                            $license_labels[] = $license['label'];
                            $license_urls[] = $license['url'];
                        }
                    }
                }

                // if there is not enough info, return the no_license_message
                if (empty($license_labels)) {
                    $result = "";
                    //$result = $header;
                    //$result .= $no_license_message;
                } else {
                    $it = new MultipleIterator();
                    $it->attachIterator(new ArrayIterator($license_labels));
                    $it->attachIterator(new ArrayIterator($license_urls));

                    $result = $header;
    
                    foreach ($it as $license) {
                        $license_label = $license[0];
                        $license_url = $license[1];
                        $result .= "<div class=\"license\"><p><a href=\"" . $license_url . "\" title=\"" .
                        $license_label . "\">" . $license_label . "</a></p>";
                    }

                    if (sizeof($license_labels) > 1) {
                        $result .= "<p><i><small>Note: " . $multiple_licenses_message . "</small></i></p></div>";
                    }
                }
            } else {
                $result = "";
                //$result = $header;
                //$result .= "<div class=\"license\"><p>" . $no_license_message . "</p></div>";
            }

            return $result;
        }
    }
}
